# Proposal: faster GPU frame after Phase 19

*2026-11-19. Not approved, not started. Estimates are from reading the code, not measured.*

## Where the time is

High, 1080p, RTX 3090, loading floor: 11.6 ms, about 8 ms of it ray tracing (ADR-0102):

| Pass | Time |
|---|---|
| Indirect light | 3.11 ms |
| Denoiser | ~1.5 ms |
| Reflections | 1.46 ms |
| ReSTIR spatial reuse | 1.00 ms |
| ReSTIR candidates | 0.80 ms |
| ReSTIR shading | 0.48 ms |
| Radiance cache update | 0.43 ms |

Already measured and ruled out: GPU-driven rendering and Hi-Z (ADR-0098), LODs (ADR-0090), async compute (ADR-0095).

On High (`gi_bounces 1`), each traced pixel of indirect light runs up to four rays and two full material shades:

1. the bounce ray, then `ShadeHit` (decals and wetness included);
2. a shadow ray from `BounceDirectLight`;
3. a second cosine ray, then another `ShadeHit`;
4. a `SeesAnchor` occlusion ray in `FindCell`, on every cache lookup whose cell already has an anchor. This also happens in the reflections' rough path and in the cache update.

## A. Same image, or nearly

1. **Count, then cut, the anchor-visibility rays** (`GlobalIllumination.slangh` `FindCell`).
   - First add a debug counter of `SeesAnchor` calls per frame.
   - Then skip the ray when the query point is close to the anchor, within a fraction of the cell size, or when the same cell was already checked on the same path.
   - Estimate: 0.3 to 0.8 ms.
2. **Shader variants per preset instead of push-constant settings.** `GiBounces()`, `GiRays()`, `spacing` and `mode` are read at run time, so the compiler can't unroll the bounce loop or drop the dead branches. Indirect light uses 72 registers and spills 32 bytes.
   - Use specialization constants or `#define` variants, and check them with `renderer.log_pipeline_statistics`.
   - Estimate: 0.1 to 0.4 ms.
3. **A packed surface buffer for ReSTIR's neighbours**, as in RTXDI's packed surface data. The candidates pass writes depth, an octahedral normal, roughness and diffuse color into one `uint4` per pixel, so each neighbour costs one load instead of a guide texel plus a full `ReadGBuffer`.
   - Estimate: 0.2 to 0.4 ms.
4. **Spatial reuse and shading in one pass**, plus RTXDI's visibility shortcut: reuse last frame's visibility for the temporal sample instead of always tracing the final shadow ray.
   - Estimate: 0.2 to 0.4 ms.
5. **A smaller vertex stream for hit shading.** Vertices are 48 B of fp32, re-fetched by `Interpolate`, `FaceNormal`, `ConeLevel` and `PassesAlphaTest`.
   - About 16 B per vertex: octahedral normal and tangent, half-precision UVs.
   - Positions from the acceleration structure with `VK_KHR_ray_tracing_position_fetch` (check that the 3090's driver exposes it).
   - The alpha test reads at the cone's mip instead of mip 0.
   - Estimate: 0.2 to 0.5 ms.
6. **A shared-memory tile for the denoiser's 1-pixel pass**, deferred in ADR-0097. Estimate: 0.2 to 0.3 ms.

## B. Changes the image slightly (judge with FLIP against the path tracer)

7. **Stop at the cache on long rays** (SHaRC's rule): keep tracing only while a path segment is shorter than the cache cell at its hit.
   - The cache looks blocky near contacts. Short rays are those contacts, so they keep the lit bounce; long rays read the cache directly.
   - Estimate: 1.0 to 1.8 ms. The largest single item.
8. **Reuse last frame's lighting where a ray's hit is on screen**, as Lumen and AMD GI-1.0 do. If an indirect or rough-reflection hit projects into last frame's G-buffer with matching depth, read the light already computed there.
   - This also adds extra bounces, which brings the image closer to the path tracer.
   - Estimate: 0.5 to 1.2 ms. Risk: lag on lights that move.
9. **Trace every other pixel's indirect light on High too** (`gi_pixels_per_ray 2`). Since ADR-0097 the denoiser accounts for repeated samples. Estimate: about 1.2 ms.
10. **High at 77 to 85% render scale with FSR 3.1 or TAAU.** The biggest knob of all: 67% saved 3.9 ms in ADR-0052. It's a preset decision for the owner.

## C. Not recommended now

- **Software ray sorting:** ADR-0102 found the warps already coherent.
- **Neural radiance caching, meshlets:** they don't fit the factory's scale.
- **FP16 arithmetic:** fp16 runs at the fp32 rate on GA102.

## Suggested order

1, 2, 7, then 3 and 4, then 5, then 8. One PR per step, measured old against new on the loading floor.

## Sources

- SHaRC integration guide: https://github.com/NVIDIAGameWorks/Spatial-Hash-Radiance-Cache/blob/main/docs/Integration.md
- RTXDI: https://raw.githubusercontent.com/NVIDIA-RTX/RTXDI/main/Doc/Integration.md and its ChangeLog
- Rearchitecting Spatiotemporal Resampling for Production (2021): https://research.nvidia.com/publication/2021-07_rearchitecting-spatiotemporal-resampling-production
- VK_KHR_ray_tracing_position_fetch: https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_ray_tracing_position_fetch.html
- GI-1.0: https://ar5iv.labs.arxiv.org/html/2310.19855
- Lumen, SIGGRAPH 2022 Advances: https://advances.realtimerendering.com/s2022/
