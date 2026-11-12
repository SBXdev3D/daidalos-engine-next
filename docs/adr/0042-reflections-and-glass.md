# ADR-0042: Ray-traced reflections and glass

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

Step 9.10 of the roadmap (ADR-0028, ADR-0041). Until now, the specular lobe of every surface reflected the indirect diffuse light of its pixel, as if every direction sent the same light: mirrors were grey, a polished floor showed nothing of the room, and metals, which have no diffuse color, received no indirect light at all. There was no glass: glTF's transmission was not read, and blended materials were drawn opaque.

Every material of the factory has the legacy roughness of 0.374 and no metal, so reflections there are soft gloss. The owner chose a roughness cutoff of 0.6, above which surfaces trace no reflection, and asked for glass over the shaft east of the hall, through which the zombie that patrols it can be seen.

## Decision

### Reflections

**A fourth pass of `GlobalIllumination`, "Reflections"**, after the indirect light pass, since it reads the radiance cache. One ray per pixel, from the pre-pass's surface, in a direction drawn from the visible normals of its GGX lobe (`SampleSpecularDirection` in `Bsdf.slang`, Heitz 2018, which the path tracer already used). Averaged and multiplied by the specular albedo, rays in that distribution are the specular reflection; the mesh pass multiplies, the denoiser averages. What a ray brings back:

- **On a smooth surface** (roughness below 0.3), sharp enough to show what it reflects: what the ray hits, shaded with its whole BSDF, so that metals show: its emission, the light of one light chosen among eight by what it would send (resampled importance sampling), with one shadow ray, and the indirect light that reaches it, from one more ray into the cache, for its diffuse and specular parts. A hit smoother than 0.1 is a mirror and reflects on, up to three surfaces.
- **On a rougher one**, whose reflection is a blur: the light the cache holds where the ray hits. Many of these hits are out of view, where no path of the cache goes. A cell worth fewer than four samples is shaded as above instead, and gets the result as a sample: reflections fill the cache where they need it, as all paths do in SHaRC.
- **Past the sky**: the sky.
- **Rougher than `renderer.reflection_roughness`** (0.6 by default): no ray. The pixel's indirect diffuse light stands in, as before.

Its output, an `Rgba16Float` image, holds the light and, in alpha, the distance to the first hit: 0 where no ray was traced. **Deleted**: the ambient specular term, the indirect light times the specular albedo, except as the fallback above. Reflections need indirect light: with `renderer.indirect_light` off, both are off.

**The denoiser follows reflections.** A reflection does not move with its surface: seen from a moving camera, the image in a mirror moves as if it were as far behind the mirror as the reflected object is in front of it. On surfaces smoother than 0.4, the temporal pass looks for the specular history where that reflected point was in the previous frame, from the hit distance, fully below 0.1 and partly in between; it falls back on the surface's motion where the point's history is not the same surface. Where the reflected point would move the history by a small fraction of the way, the second search is skipped: the factory's 0.374 does not pay for it.

### Glass

**A material is glass when its transmission is above zero** (`KHR_materials_transmission`), with its `KHR_materials_ior` and `KHR_materials_volume`: a thickness of zero is thin glass, such as a window, which bends nothing; otherwise the mesh is a closed volume, and light bends where it enters and where it leaves, losing on the way the share of each color that the attenuation color and distance give (Beer-Lambert). The base color tints what goes through. `MaterialDesc` gains `transmission`, `ior`, `thin`, `attenuationColor` and `attenuationDistance`; the material table, 96 bytes per material, the absorption per meter.

**The pre-pass and the mesh pass leave glass out.** The glass pass draws it after everything else, particles included: a copy of the frame (`CommandList::CopyTexture`, new in the RHI) is its background, and it tests the pre-pass's depth itself, since it reads it. A pixel of glass is what its surface reflects, as much as Fresnel says, plus what goes through:

- the reflection is one ray along the mirror direction;
- thin glass lets the background behind the pixel through;
- thick glass sends a ray in, bent, finds the far side with a ray that only sees glass, bends it out, and lets through what that ray finds. Past the critical angle, the ray reflects inside, four times at most.

What these rays find is read from the frame when it is on the screen and in view there, so that it is lit, denoised and fogged as the frame drew it. Otherwise it is shaded with its emission and the frame's lights, with their shadows but without indirect light; and only where it counts, since glass reflects about 4 % but at grazing angles: a reflection weaker than 10 % is not worth lighting what it shows out of view. The lights of the pixel's cluster make highlights on the glass, with their shadows.

**Glass casts no shadow, and other rays go through it.** Its instances have their own mask, `kMaskGlass`: shadow rays, indirect light, the cache's paths and reflections do not see it; the glass pass and the path tracer do. So reflections show no glass, and light through tinted glass is not tinted.

### The path tracer

**Glass in the reference**: a hit on glass reflects or goes through as Fresnel says, chosen at random; entering, light is tinted and scaled by the transmission; leaving thick glass, absorbed over the distance it crossed. Shadow rays go through glass as in the mesh pass, so that both are compared on the same terms.

### Content

- **The factory's bare metal** (lockers, the lift, pipes, gratings) is metal now, with roughness 0.3 to 0.45, in `scripts/import_legacy_level.py` and in the level: they reflect their surroundings.
- **A slab of thick glass** under the grating of the shaft east of the hall, 7 by 3.5 m and 6 cm thick, blue-green, added to `Factory.gltf` as a node named `glass` with its own buffer, `glass.bin`. It has no collision: the grating's is enough.
- **The mirror room** (`renderer::MakeMirrorRoom`, `sandbox.scene mirrors`): a mirror, a polished floor, a gold block, a white block, a window pane and a block of thick glass, lit by two lamps. The sandbox builds it like the Cornell box.

### Settings

`renderer.reflections` (on by default) and `renderer.reflection_roughness` (0.6), in `daidalos.cfg`; `renderer.debug_view reflections` shows the reflections as their rays found them, before the denoiser.

## Tests

- **RHI**: a texture copies into another.
- **Unit**: the transmission, IOR and volume of a glTF material are read; a material without them is opaque.
- **GPU, against the path tracer** (validation and synchronization validation on, as every GPU test):
  - the mirror room without glass, 128 × 96: FLIP 0.183 for the average of 128 frames and 0.192 denoised, against 0.269 without reflections;
  - the mirror room with its glass: 0.187, against 0.198 without the glass pass, which covers little of the image;
  - a camera that slides over a polished floor reflecting a bright panel, through the denoiser: 0.186 when the history follows the reflected point, 0.399 when it moves with the floor.
- **GPU**: the center of a mirror floor seen at 45 degrees, with a wall 4 m away, reflects it 4.27 m away (3√2 expected); a rough floor traces no ray.

## Consequences

- At 1920 × 1080 on the RTX 3090, in the factory over the glass: reflections take 0.91 ms, glass 0.27 ms, the frame 5.05 ms. At 2560 × 1440: 1.4 ms of reflections, and 0.5 ms more in the denoiser while it searched twice on every surface below 0.4, before the second search was kept for smoother ones.
- Mirrors and polished floors show the room; metals receive and reflect light. The factory looks almost the same, as its surfaces stay soft, but its metal parts reflect.
- Rough reflections of metals are dark: the cache holds diffuse light only.
- Reflections show no glass, and nothing through it; shadows and indirect light pass through glass untinted. Rough and frosted glass, and the share of a material that is not transmitted, wait for layered materials (Phase 11): glass is smooth, and what it does not let through is lost.
- Glass is not in the pre-pass: temporal antialiasing reprojects it with what is behind it, which suits a floor or a window, not moving glass.
- The noise of reflections on smooth surfaces, which the denoiser does not blur below a roughness of 0.1, comes from the light chosen at the hit and the ray into the cache: it averages over frames.

## Alternatives considered

- **A separate reflections class**: it would need the cache's buffers, which `GlobalIllumination` owns, and its pipelines and push constants; a fourth pass of it shares all of them.
- **Reflections only below a roughness of 0.4, as the rendering plan says**: the factory's 0.374 would be in, and a surface at 0.45 would lose its reflection of a lamp nearby. The cutoff is a setting, for the presets of 9.15.
- **Shading every reflected hit fully**: about twice the cost on the factory's rough surfaces, for a blur.
- **Glass in the pre-pass, denoised like the rest**: the pixel would need two surfaces, the glass and what is behind it, in a G-buffer that holds one. Reading the frame behind glass gives what is behind its full quality for free.
- **Tracing what thin glass shows**: shading the hit of each ray costs more and is noisier than the pixel the frame already has behind the glass.
