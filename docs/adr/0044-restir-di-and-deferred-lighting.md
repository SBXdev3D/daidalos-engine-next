# ADR-0044: ReSTIR DI, emissive lights and deferred lighting

- **Status:** Proposed
- **Date:** 2026-11-13
- **Supersedes (when accepted):** ADR-0008 (clustered light assignment on the CPU), the forward mesh pass of ADR-0022 and ADR-0039
- **Amends:** ADR-0041 (step 9.12 is split in two: ReSTIR DI here, ReSTIR GI in 9.13, ADR-0045; the steps after it move up by one)

## Context

Step 9.12 of the roadmap (ADR-0041). Direct light is drawn in the forward mesh pass: each pixel loops over the lights of its cluster, built on the CPU (ADR-0008), and casts a shadow ray towards each one that would light it. That cost grows with every light that reaches a pixel, and a room of a thousand lights, or a wall of neon tubes, cannot be lit that way. The rendering plan (section 6.2) and the engine spec ask for ReSTIR DI (Bitterli et al. 2020): each pixel picks one light among many by resampling, reuses what its neighbours and its own past found, and casts one shadow ray. NVIDIA's RTXDI is excluded by its licence (ADR-0028): the engine writes its own.

Emissive surfaces do not light anything yet: emission is only seen. The spec wants screens, neon and light panels that light the room, in the same units as lamps, which ADR-0043 set.

Secondary hits pick their light differently: the radiance cache and reflections choose one among eight candidates drawn uniformly from every light, glass loops over all of them for its reflections and uses the clusters for its surface, and particles use the clusters.

The owner approved splitting the step: ReSTIR DI and emissive lights first, ReSTIR GI next (ADR-0045), each with its own pull request and its own comparisons with the path tracer. The owner also chose deferred lighting over keeping the forward mesh pass.

## Decision

### Deferred lighting

ReSTIR needs every pixel's material before shading it: candidates are weighed by what they would send to the camera. Once direct light moves to a compute pass, the forward mesh pass would only add emission and occlusion while drawing every mesh a second time with all its textures. It is deleted.

- **The pre-pass becomes a thin G-buffer**, four targets, the RHI's limit:
  - normal and roughness (`Rgba8Unorm`, as today);
  - motion (`Rg16Float`, as today);
  - base color and metallic (`Rgba8Srgb`: sRGB applies to the color, metallic stays linear in alpha);
  - emission in nits times the exposure, and the material's occlusion in alpha (`Rgba16Float`).
- It samples every texture of the material, alpha-tested ones included. Glass stays out of it, as it stays out of the pre-pass today.
- **A lighting compute pass** writes the diffuse light per unit of diffuse color and the specular light per unit of specular albedo, with the albedo in alpha: the inputs the denoiser takes today (ADR-0039), now storage images. It adds the indirect light and the reflections, darkened by the occlusion, as the mesh pass did.
- **The composite** reads the emission and the base color from the G-buffer, and the sky's radiance where the depth is 0: `Sky.slang`'s pass at the end of the mesh pass goes.
- The normals stay in 8 bits. If the specular light of smooth surfaces bands, they move to an octahedral `Rg16` encoding.

### One list of lights

- **Analytic lights and emissive triangles in one buffer.** A light is a point, spot or directional light as today, or a triangle of an emissive draw.
- **An emissive material is a light** by default. glTF material `extras` `{"light": false}` makes its emission only seen, by the camera and by reflection rays; it lights nothing. `MaterialDesc::emissiveLight`, true by default, carries it.
- **Emission per triangle.** When an emissive mesh and material are first drawn together, a compute pass averages the emissive texture over each triangle, from the mip whose texel matches the triangle's area in UV space, times the emissive factor. The result is read back once. A triangle's world-space corners are written each frame by a compute pass for every emissive draw, so moving emissive meshes stay lights.
- **Sampling by power, in two levels.** On the CPU, each frame, an alias table (Walker 1977, Vose 1991) over the analytic lights and the emissive draws, by power: a light's flux from its candela and cone, a draw's from its triangles' area and emission, times its scale. Within a draw, a second alias table over its triangles, built once from the read-back emission. A flickering light only changes its power. The probability of picking a light is known exactly, so sampling stays unbiased however rough the powers are.
- **A point on a light.** On a triangle, uniform over its area; a sphere is shaded from its center, as today, and its shadow ray aims at a point of its disk (ADR-0039).
- **Stable identities.** A reservoir keeps its light from one frame to the next. Analytic lights are named by their entity, emissive triangles by their draw's entity and their index; each frame the CPU uploads a map from last frame's light indices to this frame's, and a light that is gone ends its reservoirs.

### ReSTIR DI

Four compute passes at full resolution, after the G-buffer:

1. **Candidates.** Eight lights drawn from the alias table, each with a point on it, weighed by the light it would send to the camera, unshadowed: illuminance times the BSDF and the cosine, in luminance (resampled importance sampling, Talbot et al. 2005). One is kept, and **a shadow ray tests it before any reuse**: indoors, most candidates are behind a wall, and an occluded sample would spread to the neighbours.
2. **Temporal reuse.** The reservoir of the same surface in the previous frame, found through the motion vectors with the denoiser's depth and normal tests, its light mapped to this frame's index. Its sample count is capped at 20, so that the past fades.
3. **Spatial reuse.** Five neighbours within 30 pixels, on the same surface by depth, normal and roughness. Reservoirs are combined with generalized RIS and pairwise MIS weights (Lin et al. 2022; Wyman et al., "A Gentle Introduction to ReSTIR", 2023).
4. **Shading.** One shadow ray towards the chosen point; the light's diffuse and specular parts are written for the denoiser.

- **Biased about visibility by default**, as the rendering plan says: neighbours' samples are not re-tested from the pixel when they are combined. `renderer.restir_unbiased` adds those rays, to compare with the path tracer.
- **Reservoirs:** 16 bytes per pixel (light index, the point's two coordinates in 16 bits each, weight, sample count), two buffers that swap each frame: about 66 MB at 1080p.
- **`renderer.restir_spatial`** turns spatial reuse off for comparisons. `renderer.debug_view lights` shows each pixel's chosen light, one hue per light.

### Every other place that lights a point

The radiance cache's paths, reflections, glass and particles draw their candidates from the same alias table instead of uniformly, still eight candidates and one shadow ray, with one shared function. Glass loses its loop over every light, particles their clusters.

### Emissive meshes are counted once

- A camera ray sees every emissive surface.
- A ray of the cache or of indirect light that hits an emissive light adds no emission: light sampling counts it. One that hits emission that is not a light adds nothing either.
- A reflection ray sees emission that is not a light. Emission that is a light reaches reflections through the specular part of light sampling.
- **The path tracer** counts an emissive triangle only when a ray hits it, and analytic lights only by sampling them: each light is counted by one strategy, so the reference stays unbiased without multiple importance sampling.

### Deleted

`LightClusters` and its tests, the cluster buffers and their `FrameData` fields, `Mesh.slang` and the mesh pass's draws, the sky's pass, glass's light loop.

## Tests

- **Unit**: the alias table returns each entry with its probability (chi-squared over a million draws); power of point, spot and directional lights; the light index map when lights are added, removed and reordered; the `light` extras of glTF materials.
- **GPU**:
  - the G-buffer's targets for a known material;
  - a triangle's emission averaged from its texture;
  - every existing comparison with the path tracer measured again: direct shadows, soft shadows, the Cornell box, the mirror room;
  - **a room of 1,000 point lights** of random colors and emissive panels on two walls, one of them not a light: the denoised frame against the path tracer (FLIP), with and without spatial reuse, and biased against unbiased;
  - one light alone is lit exactly as before the change.
- Validation and synchronization validation stay clean.

## Consequences (to be measured)

- **Budget:** ReSTIR DI under 1 ms at 1080p on the RTX 3090, in the factory and in the room of 1,000 lights.
- The cost of direct light no longer grows with the lights that reach a pixel.
- Where several lights overlap, the light itself becomes noisy, not only its shadows: today a pixel sums its lights exactly. If the factory looks noisier than before, the few strongest lights are shaded exactly and ReSTIR samples the rest.
- On mirror-smooth surfaces, a large emissive panel sampled by lights only gives a noisy highlight. If the room of lights shows it, a ninth candidate drawn from the specular lobe joins the eight.
- Memory: two G-buffer targets more, the mesh pass's targets become storage images, reservoirs about 66 MB at 1080p.
- One raster pass over the scene instead of two.

## Alternatives considered

- **Keeping the forward mesh pass**, with one more pre-pass target for the material: less change, but a second raster pass with every texture, to add emission.
- **A light BVH** (Conty Estevez and Kulla 2018): better candidates in a level with thousands of lights far apart, and a build and traversal to write. ReSTIR's reuse finds the light that matters within a few frames, and the factory has 25 lights.
- **ReGIR** (Boksansky et al. 2021), a world-space grid of reservoirs as the source of candidates: the next step if the room of 1,000 lights, or a level, measures that power sampling wastes its candidates.
- **Keeping the clusters as the source of candidates**: lights reaching a cluster are not lights that matter, and emissive triangles would need clusters of their own.
- **A candidate from the BSDF from the start**: kept for when the emissive panels show they need it.
- **Unbiased by default**: twice the rays for a difference the denoised image may not show; a setting compares them.
