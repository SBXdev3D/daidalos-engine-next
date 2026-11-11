# ADR-0035: Ray-traced shadows for every light

- **Status:** Accepted, amended by ADR-0039 (lights have a radius and their shadows are soft, without a shadow pass of their own)
- **Date:** 2026-11-07
- **Supersedes:** ADR-0023 (shadow maps); amends ADR-0026 (its shadow settings are gone)

## Context

Step 9.5 of ADR-0028. Shadow maps gave a shadow to the four spot lights nearest the camera that asked for one, in a 2048-texel atlas. Point lights and every other light had none: their light went through walls. The acceleration structures of step 9.3 let a pixel ask directly whether a light reaches it, and the path tracer of step 9.4 shows what the answer should be.

## Decision

- **Every light casts shadows, always.** The mesh pass's pixel shader casts one ray query towards each light of its cluster that would light it; the light counts only if nothing that casts shadows stands in between. `Light::castsShadows`, the level's `light_shadows` flags and the settings `renderer.shadow_lights` and `renderer.shadow_resolution` are gone; an old `user.cfg` that still sets them gets a warning.
- **Hard shadows.** Soft shadows need random rays and a denoiser, which step 9.8 brings, with a pass of its own for the shadows; building that pass now would have meant designing it twice.
- **What a shadow ray sees**: draws that cast shadows (an instance mask, so the first-person weapons cast none), alpha-tested surfaces through their holes (tested during traversal), skinned draws as drawn (they share their vertices with rays, ADR-0033). The ray starts just off the surface, along the triangle's normal from the derivatives of the position, and stops 10 cm short of the light, as the shadow maps ignored what was that close to it.
- **One copy of the ray code.** Reading a hit's triangle and material, the alpha test and the shadow ray are in `RayQueries.slangh`, which the mesh pass and the path tracer include: rasterized shadows and the reference agree by construction. It is an include rather than a Slang module because it reads the bindless arrays that the shaders declare.
- **Deleted**: the shadow atlas, its pipeline and `ShadowMap.slang`, the shadow data, the choice of shadowed lights and its tests.
- **FLIP** (NVIDIA's perceptual image difference, BSD 3-Clause) is in `third_party/flip`, for the tests only. The path tracer can send every path through the pixel's center (`PathTracerSettings::jitter`), as the mesh pass samples, so that a comparison measures shading rather than edges.

## Consequences

- The Cornell box's direct light from the mesh pass and from one bounce of the path tracer differ by a mean FLIP of 0.014; against a path tracer without shadows, by 0.113. A GPU test holds them under 0.03 and apart by four times.
- GPU time in the factory, 1280×720, RTX 3090, Release, 29,700 frames, two runs each: the mesh pass takes 0.008 to 0.015 ms more, and the 0.044 ms shadow-map pass is gone: the frame is about 0.02 ms faster.
- Light no longer goes through walls, which changes how the factory looks where a lamp stands behind one.
- Particles still neither cast nor receive shadows; volumetric light with ray-traced shadows is step 9.13.

## Alternatives considered

- Honouring the level's `light_shadows=false`: keeps the light that leaks through walls, for a cost that turned out not to matter.
- A shadow pass that writes a mask per light for the mesh pass to read: what soft shadows will need in step 9.8, built then rather than twice.
- Keeping shadow maps as an option: ADR-0028 keeps no raster fallback.
