# ADR-0039: An own spatiotemporal denoiser, and soft shadows

- **Status:** Accepted
- **Date:** 2026-11-11
- **Amends:** ADR-0028 (a concrete denoiser, not an `IDenoiser` interface), ADR-0035 (lights have a radius; no shadow pass of their own)

## Context

Step 9.8 of ADR-0028. Lights were points: every shadow was hard, and a ray per light told a pixel whether the light reached it (ADR-0035). Real lamps have a size, and their shadows soften with distance. A ray towards a random point of the lamp gives that softness on average, and one frame of it is noise: a denoiser has to average it away.

ADR-0035 expected a shadow pass writing a mask per light, filtered like NVIDIA's SIGMA. That works for a few lights, and 9.11 replaces it: ReSTIR DI picks one light per pixel and gives noisy light, not masks. Its denoiser filters radiance. So do those of 9.9 (diffuse global illumination) and 9.10 (reflections). A denoiser of masks would be thrown away after three steps; a denoiser of radiance is the one every later step feeds. NVIDIA's NRD is excluded by its licence (ADR-0028), so the engine writes its own, from the published algorithms.

## Decision

### Lights with a radius

- **`renderer::Light::radius` and `scene::Light::radius`**, in meters: the light is a sphere. The renderer's default is 0, a point with hard shadows; `scene::Light`'s is 5 cm, a bulb, so every lamp of the factory has soft shadows without code.
- **Shading still uses the light's center.** Only the shadow ray aims elsewhere: at a point of the disk of the sphere's radius that faces the pixel, uniform on it (`SampleLightPoint` in `RayQueries.slangh`). The path tracer aims its shadow rays the same way, so the reference has the same soft shadows. Area lights that also change the shading wait for content that needs them.
- **The random numbers**: over frames, the R2 sequence, which covers the square evenly whatever the number of frames; over pixels, shifted by interleaved gradient noise, which spreads evenly over neighbours; each light of a pixel shifted further. `FrameData::frameIndex` counts the frames.

### What the mesh pass writes

- **Four targets** (the RHI's limit, ADR-0037): the base (ambient light and emission, in the color format), the lights' diffuse light per unit of diffuse color (`kDiffuseFormat`, RGBA16F; alpha is 1 where a ray aimed at a random point), their specular light divided by the luminance of the surface's specular albedo, with that albedo in alpha (`kSpecularFormat`, RGBA16F), and the diffuse color (`kAlbedoFormat`, sRGB 8 bits). Without the surface's colors, the denoiser blurs light and not textures.
- **`EvaluateBsdfTerms`** gives the BSDF's diffuse part per unit of diffuse color and its specular part; `EvaluateBsdf` adds them. **`SpecularAlbedo`**, Karis's fit that the ambient term already used, moves to `Bsdf.slang`.
- **The pre-pass writes the roughness** in the alpha of its normals.
- **The fog leaves the mesh pass** for the composite. The sky writes zero light to the other targets.
- **`MeshPass::CreateShadingTargets`, `ShadingRendering` and `AddComposite`** give the renderer and the tests the same targets and the same composite.

### The denoiser

`Denoiser` (`renderer/src/Denoiser.hpp`) filters the diffuse and the specular light together, in two compute shaders. It is a concrete class: its inputs (the light, depth, normals and roughness, motion) and outputs (the filtered light) are the seam where another denoiser would fit, and an interface with one implementation would be the abstraction OVERVIEW warns against.

1. **Temporal accumulation** (`DenoiseTemporal.slang`): the history is found where the motion vectors say the pixel was, from its four nearest pixels, each kept only if it is the same surface: its linear depth near the one expected there, or the one here for a moving object, within 5 %, and its normal within 25 degrees. The light is averaged with up to 32 frames. The moments of its luminance give the variance of one frame's light, or, below 4 frames, its neighbours on the same surface do, where the pixel's light may be noisy; elsewhere the variance is 0. The variance written is that of the average: one frame's, divided by the frames.
2. **Bounding the history**: the history is clamped to the range of the 3 × 3 neighbourhood of this frame's light, widened by one standard deviation of the history's own noise. Where the light was not noisy, a light that goes out, or a shadow that moves away, shows in the next frame; in a penumbra, whose neighbours are often all lit or all dark in one frame, the history stays, and the average is not pushed towards either.
3. **Spatial filter** (`DenoiseSpatial.slang`): five iterations of an à-trous wavelet, 5 × 5 taps spread 1, 2, 4, 8 and 16 pixels apart. A neighbour counts as much as it lies on the pixel's plane, faces the same way (cosine to the power 64), and its luminance is within four standard deviations of the expected noise, from the variance blurred over 3 × 3 pixels. Where the variance is 0, only neighbours of the same luminance count: hard shadows stay sharp. **A pixel takes part in an iteration only while step² × frames ≤ 32**: the average of n frames is as noisy as one frame blurred over n pixels, so a pixel with a full history only gets the narrowest iteration, and a pixel just uncovered the widest three.
4. **The history is the unfiltered average.** Feeding the spatial filter's output back, as SVGF does, blurs the image a little more every frame. The first version did, with every iteration for every pixel and the variance of one frame rather than of the average: the Cornell box scored a FLIP of 0.078 instead of 0.032.
5. **Specular light** is blurred less on smoother surfaces (not below a roughness of 0.1, fully from 0.4), only with neighbours of a similar roughness. It is reprojected with the surface's motion; reflections need a reprojection of their own, which 9.10 brings.
6. **The history starts over** after a resize, a cut (`SceneView::cut`), a frame without the denoiser, or `Reset`.

### The frame

Pre-pass, ambient occlusion, particle simulation, **Meshes** (four targets), **Denoiser** (one temporal and five spatial dispatches), **Composite** (base + diffuse × albedo + specular × specular albedo, then the fog, into the HDR target), particles, temporal resolve, tonemap. Without a camera, tonemapping shows the base, which the mesh pass cleared to the fog's color.

### The RHI

- **Storage images in `Rgba16Float`** as well as `Rgba32Float`. Shaders declare one array per format at binding 4, each with its format; a texture is accessed through the array of its own. The denoiser's textures are half floats: twelve full-float ones would double its memory and bandwidth.

### Settings

- **`renderer.denoiser`** (`true` by default), `Renderer::SetDenoiser`: off shows the light as the rays found it, which is what the denoiser is compared with. The player's menu does not offer it.

## Tests

- **RHI**: a compute shader writes a half-float storage image.
- **Unit**: `ExtractScene` carries a light's radius, and a `scene::Light` is 5 cm by default.
- **GPU, denoiser** (`DenoiserTest`, a floor, a panel and a spot light of a meter's radius):
  - a hard shadow goes through unchanged, on the first frame and after eight;
  - a soft shadow's edge differs from one frame to the next alone; after 16 denoised frames it is within 6 levels of 255, on average, of the average of 64 noisy frames, and the next frame changes ten times fewer pixels than the noise did;
  - a light switched off is gone from the next frame;
  - a panel that jumps away leaves no shadow behind in the next frame;
  - after `Reset`, a frame comes out as from a denoiser that never saw another.
- **GPU, against the path tracer** (the Cornell box with a light of 30 cm radius, 96 × 96): the mean FLIP of the average of 256 noisy frames is 0.025, and of the 32nd denoised frame 0.032; one noisy frame scores 0.084, and hard shadows 0.083. The test holds the average under 0.03 and the denoised frame under 0.04 and under both others. The average still differs from the reference where a pixel's center falls on the seam of two walls.
- Every existing test of the mesh pass, the path tracer and the resolve goes through the composite; the mesh pass tests compose without the denoiser. Validation and synchronization validation stay clean.

## Consequences

- Shadows are soft, wider the farther they fall from what casts them, and every lamp of the factory gets them.
- GPU time at 1920 × 1080 in Release on the RTX 3090, the factory, 6,000 frames per run, two runs each: the denoiser takes 0.53 to 0.78 ms, and the frame 1.04 to 1.49 ms with it, against 0.57 to 0.62 ms without. The clock of the GPU changes between runs more than the passes do.
- Memory at 1080p: the denoiser's twelve half-float textures take about 200 MB (two histories and two work textures per signal, two guides and two moments), and the mesh pass's four targets about 58 MB more than its one. Fifteen of the 64 storage image slots are in use.
- A soft shadow takes a few frames to settle after it is uncovered, and its penumbra lags when its light moves: the history only follows at once where the light is not noisy.
- Every later step feeds the same denoiser: 9.9 adds indirect light to the diffuse signal, 9.10 reflections to the specular one, and 9.11 replaces where the direct light comes from.

## Alternatives considered

- **A shadow mask per light, SIGMA's way**: as many signals as shadowed lights, and nothing left of it once ReSTIR DI picks a light per pixel.
- **The ratio estimator** (Heitz et al. 2018): exact unshadowed light times the denoised ratio of shadowed to unshadowed noisy light. Elegant, and also gone with ReSTIR DI.
- **An `IDenoiser` interface**, as ADR-0028 planned: one implementation, and the RHI already chose concrete classes.
- **SVGF's feedback of the first spatial iteration**: blurs further every frame (see above).
- **A fast history to bound the slow one**, ReLAX's way: more textures, for a problem the tests did not show once the neighbourhood's range was widened by the history's noise. It is the next thing to try if penumbras lag visibly in play.
- **Blue noise textures**: an asset and its licence to track, for a gain the temporal average mostly hides.
- **Lighting moved to a compute pass over a G-buffer**: what ReSTIR DI will need in 9.11, and not before.
