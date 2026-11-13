# ADR-0044: ReSTIR DI, emissive lights and deferred lighting

- **Status:** Accepted
- **Date:** 2026-11-13
- **Supersedes:** ADR-0008 (clustered light assignment on the CPU), the forward mesh pass of ADR-0022 and ADR-0039
- **Amends:** ADR-0041 (step 9.12 is split in two: ReSTIR DI here, ReSTIR GI in 9.13, ADR-0045; the steps after it move up by one), ADR-0024 (the pre-pass becomes the G-buffer)

## Context

Step 9.12 of the roadmap (ADR-0041). Direct light was drawn in the forward mesh pass: each pixel looped over the lights of its cluster, built on the CPU (ADR-0008), and cast a shadow ray towards each one that would light it. That cost grows with every light that reaches a pixel, and a room of a thousand lights, or a wall of neon tubes, cannot be lit that way. The rendering plan (section 6.2) and the engine spec ask for ReSTIR DI (Bitterli et al. 2020): each pixel picks one light among many by resampling, reuses what its neighbours and its own past found, and casts one shadow ray. NVIDIA's RTXDI is excluded by its licence (ADR-0028): the engine writes its own.

Emissive surfaces lit nothing: emission was only seen. The spec wants screens, neon and light panels that light the room, in the same units as lamps, which ADR-0043 set.

Secondary hits picked their light differently: the radiance cache and reflections chose one among eight candidates drawn uniformly from every light, glass looped over all of them for its reflections and used the clusters for its surface, and particles used the clusters.

The owner approved splitting the step: ReSTIR DI and emissive lights first, ReSTIR GI next (ADR-0045), each with its own pull request and its own comparisons with the path tracer. The owner also chose deferred lighting over keeping the forward mesh pass.

## Decision

### Deferred lighting

ReSTIR needs every pixel's material before shading it: candidates are weighed by what they would send to the camera. Once direct light moves to a compute pass, the forward mesh pass would only add emission and occlusion while drawing every mesh a second time with all its textures. It is deleted.

- **The pre-pass becomes a thin G-buffer**, four targets, the RHI's limit:
  - normal and roughness (`Rgba16Float`, normal times 0.5 plus 0.5): in 8 bits, the light of one lamp on the Cornell box differed from the path tracer's by FLIP 0.026, against 0.014 in half floats and 0.009 for the forward pass's exact normals;
  - motion (`Rg16Float`, as before);
  - base color and metallic (`Rgba8Srgb`: sRGB applies to the color, metallic stays linear in alpha);
  - emission in nits times the exposure, and the material's occlusion in alpha (`Rgba16Float`).
- It samples every texture of the material, alpha-tested ones included. Glass stays out of it, as it stayed out of the pre-pass. Shader hot reload now replaces the G-buffer's shaders (`GBuffer.slang`).
- **A lighting compute pass** writes the diffuse light per unit of diffuse color and the specular light per unit of specular albedo, with the albedo in alpha: the inputs the denoiser takes (ADR-0039), now storage images. It adds the indirect light and the reflections, darkened by the occlusion, as the mesh pass did.
- **The composite** reads the emission and the base color from the G-buffer, and draws the sky, or the fog's color, where the depth is 0: `Sky.slang` is gone.
- Compute passes find a pixel's own orientation from the depth of its neighbours, on the side nearer in depth, and from the side that exists at the image's edges.

### One list of lights

- **Analytic lights and emissive triangles in one list.** A light is a point, spot or directional light, or a triangle of an emissive draw: the analytic lights come first, then the triangles.
- **An emissive material is a light** by default. glTF material `extras` `{"light": false}` makes its emission only seen: by the camera, through glass and in sharp reflections; it lights nothing. `MaterialDesc::emissiveLight`, true by default, carries it. Glass is never a light.
- **Emission per triangle.** When an emissive mesh and material are first prepared together, a compute pass averages the emissive texture over each triangle, at the centroids of the 64 equal triangles it cuts it into (a point on an edge would read the other side of a texture that repeats), from the mip whose texels match the share of the triangle each point stands for, times the emissive factor. The CPU reads it back `kFramesInFlight` frames later; until then the draw lights nothing. Each frame, a compute pass writes the corners of every triangle of every emissive draw in world space, so moving and skinned emissive draws stay lights; every pass that samples lights waits for it.
- **Sampling by power, in two levels.** On the CPU, each frame, an alias table (Walker 1977, Vose 1991) over the analytic lights and the emissive draws, by power: a light's flux from its candela and cone (a directional light counts what it sends through a disk of 10 m, a guess), a draw's from its triangles' area and emission, times the scale of its area, pi and its sides. Within a draw, a second alias table over its triangles, built once from the read-back emission. The probability of picking a light is known exactly, so sampling stays unbiased however rough the powers are.
- **A point on a light.** On a triangle, uniform over its area; a sphere is shaded from its center, as before, and its shadow ray aims at a point of its disk (ADR-0039), placed only when the ray is cast.
- **Stable identities.** `SceneView` lights and draws carry their entity as a `SceneId`; without one, their position in the view names them. Each frame, the CPU maps last frame's lights to this frame's by source: an analytic light by its name, an emissive draw by its name, mesh and material, its triangles with it if it has as many. The map also says which of this frame's lights are new.

### ReSTIR DI

Four compute passes at full resolution, after the G-buffer:

1. **Candidates.** Eight lights drawn from the alias table, each with a point on it, weighed by the light it would send to the camera, unshadowed: illuminance times the BSDF and the cosine, in luminance (resampled importance sampling, Talbot et al. 2005). One is kept, and **a shadow ray tests it before any reuse**: indoors, most candidates are behind a wall, and an occluded sample would spread to the neighbours. The pass also writes the pixel's face normal and depth.
2. **Temporal reuse.** The reservoir of the same surface in the previous frame, found through the motion vectors with tests on face normal and depth, its light mapped to this frame's index. Its confidence is capped at 20, so that the past fades. The two are weighed by confidence alone, being the same surface, except for a light that is new this frame, which only the new reservoir can have found: it takes its whole share, and a light that comes on shows at once. Without this, a light that appeared faded in over twenty frames.
3. **Spatial reuse.** Three neighbours within 30 pixels, on the same surface by face normal, depth, normal and roughness, read from the face normals and depths the candidates wrote. Reservoirs are combined with generalized RIS and pairwise MIS weights (Lin et al. 2022; Wyman et al., "A Gentle Introduction to ReSTIR", 2023). Three neighbours gave the same images as five in every comparison, for 0.5 ms less at 1080p.
4. **Shading.** One shadow ray towards the chosen point; the light's diffuse and specular parts are written for the denoiser. A chosen light found hidden is dropped from the reservoir, so that the next frame does not reuse it.

- **Biased about visibility by default**, as the rendering plan says: neighbours' samples are not re-tested from the pixel when they are combined, and a reservoir that holds no light, its light hidden, is passed over rather than weighed. `renderer.restir_unbiased` adds those rays and weighs every reservoir, to compare with the path tracer. The biased and unbiased spatial passes are separate shaders: code for shadow rays, even unused, slowed the biased one by half.
- **Reservoirs:** 16 bytes per pixel (light index, the point's two numbers in 16 bits each, weight, confidence), in two buffers: the candidates and temporal reuse write one, the spatial reuse the other, which the shading uses and the next frame's temporal reuse reads. About 66 MB at 1080p.
- **`renderer.restir_spatial`** turns spatial reuse off for comparisons. `renderer.debug_view lights` shows each pixel's chosen light, one hue per light.

### Every other place that lights a point

The radiance cache's paths, reflections, glass and particles draw their candidates from the same alias table instead of uniformly, still eight candidates and one shadow ray, with one shared function (`Lights.slangh`). Glass loses its loop over every light, particles their clusters; particles now cast shadow rays, and directional lights light them.

### Emission is counted once

- The camera sees every emissive surface, through the G-buffer.
- The radiance cache gathers no emission: emissive lights reach it through light sampling, and emission that is not a light lights nothing.
- **Sharp reflections show emissive lights.** A pixel smoother than `min(0.3, renderer.reflection_roughness)` traces a sharp reflection, which shades what it hits, emission included; its light sampling leaves out the specular light of emissive triangles there (`FrameData::sharpReflections`). A mirror that reflects on does the same at each bounce, and glass, a mirror too, shows emissive lights in its reflection and leaves them out of its highlights. Rougher pixels get the specular light of emissive triangles from light sampling, and their reflection rays read the cache. A near-mirror floor reflecting a panel scored FLIP 0.95 with light sampling alone, 0.184 with this split (0.186 before the step, when the panel was only seen).
- **The path tracer** counts an emissive triangle only when a ray hits it, and analytic lights only by sampling them: each light is counted by one strategy, so the reference stays unbiased without multiple importance sampling. It shows emission that is not a light only where the engine does: straight from the camera, through glass and after a sharp reflection. `PathTracerSettings::directOnly` keeps the direct light alone: lights sampled at the first surface, emission at the second.

### Deleted

`LightClusters` and its tests, the cluster buffers and their `FrameData` fields, `Mesh.slang` and the mesh pass's draws, `Sky.slang`, glass's light loop.

## Tests

- **Unit**: the alias table returns each entry with its probability (chi-squared over a million draws); power of point, spot and directional lights; the light map when lights are added, removed and reordered, and when a draw's triangles change; the `light` extras of glTF materials; entities name their lights and draws.
- **GPU**, all with validation and synchronization validation:
  - one light alone is lit as the path tracer lights it, for a point and a spot light (the weight is exactly one);
  - the emission of a triangle averaged from its texture;
  - an emissive panel lights the Cornell box as the path tracer sees it; a panel that is no light glows and lights nothing;
  - every existing comparison with the path tracer, measured again: direct shadows, soft shadows, the Cornell box, the mirror room, glass, moving reflections;
  - **a room of 1,000 point lights** of random colors and emissive panels on two walls, one of them not a light: the denoised frame against the path tracer's direct light (FLIP), with and without spatial reuse, and biased against unbiased.

## Measured

On an RTX 3090. FLIP against the path tracer; lower is nearer.

| Scene | Before | After |
|---|---|---|
| One hard light, Cornell box | 0.009 | 0.014 |
| Soft shadows, average of frames | 0.014 | 0.021 (unbiased) |
| Soft shadows, denoised | 0.022 | 0.038; 0.029 unbiased |
| Cornell box with indirect light, averaged / denoised | 0.165 / 0.175 | 0.150 / 0.159 |
| Mirror room, averaged / denoised | 0.247 / 0.255 | 0.234 / 0.246 |
| Glass in the mirror room | 0.253 | 0.244 |
| Moving camera over a polished floor | 0.186 | 0.184 |
| Emissive panel, averaged / denoised | (emission lit nothing) | 0.087 / 0.126; two references differ by 0.062 |
| 1,000 lights, denoised; without spatial reuse; unbiased | | 0.224; 0.225; 0.227 |
| 1,000 lights, one frame; without spatial reuse | | 0.356; 0.379 |

Frames of the 1,000 lights averaged are as bright as the path tracer's, within 3%; two references of other seeds differ by 0.100.

GPU time at 1920 x 1080 (`DISABLED_TimeTheDirectLightOfAThousandLightsAt1080p`, and the developer tools in the factory):

| | Room of 1,000 lights | Factory |
|---|---|---|
| G-buffer | 0.16 ms | 0.20 ms |
| ReSTIR DI and lighting | 2.15 ms: candidates 0.80, temporal 0.23, spatial 0.92, shading 0.20 | 1.10 ms |
| Before: pre-pass and mesh pass | | 0.57 ms |

## Consequences

- **The budget is missed.** ReSTIR DI was to take under 1 ms at 1080p; it takes 1.10 ms in the factory and 2.15 ms in the room of 1,000 lights. Most of it is evaluating lights: about 0.1 ms per light weighed per pixel at 1080p, eight in the candidates, three per neighbour in the spatial reuse. What would cut it, in order of what it would save: light presampling into small tiles of lights drawn by power, which candidates then read coherently (RTXDI's approach, written anew); fewer candidates, which measured worse (four: 0.242 against 0.224 denoised, for 0.33 ms less); half-resolution reservoirs. Left for a decision.
- The cost of direct light no longer grows with the lights that reach a pixel.
- Where several lights overlap, the light itself becomes noisy, not only its shadows: a pixel no longer sums its lights exactly. The factory does not look noisier after the denoiser; if it did, the few strongest lights would be shaded exactly and ReSTIR would sample the rest.
- A specular-lobe candidate, planned for mirror-smooth surfaces, was built and dropped: on a near-mirror it still left the panel's reflection dim and streaky, and the split by roughness above shows it exactly.
- Memory: the normals double to 8 bytes per pixel, one G-buffer target more for the material, the lighting's targets become storage images, reservoirs about 66 MB at 1080p.
- One raster pass over the scene instead of two.

## Alternatives considered

- **Keeping the forward mesh pass**, with one more pre-pass target for the material: less change, but a second raster pass with every texture, to add emission.
- **A light BVH** (Conty Estevez and Kulla 2018): better candidates in a level with thousands of lights far apart, and a build and traversal to write. ReSTIR's reuse finds the light that matters within a few frames, and the factory has 25 lights.
- **ReGIR** (Boksansky et al. 2021), a world-space grid of reservoirs as the source of candidates: the next step if a level measures that power sampling wastes its candidates.
- **Keeping the clusters as the source of candidates**: lights reaching a cluster are not lights that matter, and emissive triangles would need clusters of their own.
- **A candidate from the BSDF**: built, measured and dropped, see above.
- **Unbiased by default**: twice the rays for a difference the denoised image does not show (0.227 against 0.224 in the room of 1,000 lights); a setting compares them.
- **Octahedral normals in 16 bits**, which the plan held in reserve: half floats needed no change in the passes that read normals.
