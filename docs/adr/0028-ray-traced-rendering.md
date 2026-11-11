# ADR-0028: Ray-traced rendering

- **Status:** Accepted, amended by ADR-0039 (a concrete denoiser instead of an `IDenoiser` interface) and ADR-0040 (32-bit cache keys)
- **Date:** 2026-11-05

## Context

Phase 7 built a rasterized renderer: clustered forward lights, four shadow maps, SSAO, a flat ambient term from above and below. It lights the factory, but indoor scenes are dominated by bounced light, have many lights, many glossy surfaces and thin walls, and these techniques approximate all four poorly. The owner wants the engine to move to high-end rendering, and wrote `docs/daidalos-engine-next-rendering-plan.md` as the target. This ADR records what is taken from that plan, what is changed, and what is left out.

## Decision

- **Hardware ray tracing is mandatory.** The minimum GPU is an NVIDIA RTX 30xx class card (AMD RDNA 2, Intel Arc A as equivalents). The engine refuses to start, with a clear message, on a device without `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_ray_tracing_pipeline` and `VK_KHR_ray_tracing_position_fetch`. *Amended by ADR-0031:* only acceleration structures and ray queries are required; the path tracer uses ray queries in compute, and a step that needs the other two adds them.
- **No raster fallback.** Each step of Phase 9 deletes the technique it replaces in the same pull request. The game stays playable at every step.
- **Vulkan 1.4** replaces Vulkan 1.3 as the baseline (ADR-0007 amended).
- **Slang** replaces HLSL with DXC (ADR-0009 superseded). Every shader moves in one pull request. `slangc` comes from vcpkg and runs at build time and for hot reload, as DXC does today: the engine does not link the compiler.
- **A minimal render graph** with automatic barriers, debug labels and per-pass GPU timings. Resource aliasing and async compute only when a measurement asks for them.
- **A reference path tracer** comes before the real-time techniques. It is the oracle of the golden-image tests: real-time output is compared with it (FLIP).
- **Own implementations only** of the denoiser, the radiance cache and ReSTIR. NVIDIA's NRD, SHaRC, RTXDI and DLSS are under the NVIDIA RTX SDKs License, whose section 4(e) forbids any use that would make the SDK subject to a licence allowing derivative works, which CC BY-NC-SA 4.0 (ADR-0013) does. Their algorithms are published and are implemented from the papers.
- **Upscaling with FSR 2** (MIT), behind an engine interface, after an own TAA. XeSS (binary only, Intel licence) is possible later; DLSS is excluded with the other NVIDIA SDKs.
- **Lighting is fully dynamic**: no baking. Shadows, indirect light, reflections and volumetric shadows are ray traced.
- **Deferred until a measurement asks for them**: meshlets, mesh shaders, the visibility buffer, Hi-Z occlusion, LODs, portals. The factory's meshes take 0.74 ms of GPU time at 720p; ray-traced lighting, not rasterization, will be the cost. The forward pipeline with its depth and normal prepass stays, and the prepass also writes what the denoiser needs (normal, roughness, linear depth, motion vectors).
- **Deferred until content needs them**: layered material lobes (clear coat, sheen, iridescence, anisotropy, subsurface), IES profiles and area lights. One BSDF implementation is shared by raster shading, ray hits and the path tracer.
- **Out**: the neural tier (the only test GPU, an RTX 3090, has no cooperative vectors), `VK_EXT_descriptor_heap` (one binding model, descriptor indexing, until it is promoted to KHR), vendor latency and frame generation SDKs.
- **Target**: 60 fps at 1080p on the RTX 3090.

| Step | Adds | Deletes |
|---|---|---|
| 9.1 | Vulkan 1.4, ray tracing required, Slang | Vulkan 1.3 path, DXC |
| 9.2 | Render graph | hand-written barriers |
| 9.3 | Acceleration structures: compacted BLAS, refit for skinned meshes, TLAS each frame (compaction deferred, ADR-0033) | vertex-shader skinning |
| 9.4 | Reference path tracer, Cornell box, golden-image tests | — |
| 9.5 | Ray-query shadows for every light | shadow atlas (ADR-0023) |
| 9.6 | Sky: cube textures, the sky where nothing is drawn and for rays that leave the scene (added 2026-11-07, ADR-0036) | the fog color as a backdrop |
| 9.7 | Motion vectors, jitter, TAA | MSAA, FXAA (ADR-0026) |
| 9.8 | Own spatiotemporal denoiser, soft shadows | — |
| 9.9 | Diffuse GI: own hash-grid radiance cache and a ray-traced bounce | SSAO, hemisphere ambient (ADR-0024) |
| 9.10 | Ray-traced reflections, glass | ambient specular (ADR-0022) |
| 9.11 | ReSTIR DI with emissive triangles, ReSTIR GI | CPU light clustering (ADR-0008) |
| 9.12 | FSR 2, auto-exposure, bloom, AgX, HDR output | PBR Neutral tone mapping |
| 9.13 | Froxel volumetrics with ray-traced shadows | exponential fog |

ADR-0008, 0009, 0023 and 0024, and the antialiasing part of 0026, become superseded when their step merges.

## Consequences

- The engine no longer runs on GPUs without hardware ray tracing, and no longer on SwiftShader. Lavapipe supports acceleration structures and ray queries since Mesa 24.1, very slowly: whether the Linux CI runner's Mesa is recent enough is checked in 9.1. If it is not, ray-traced GPU tests run only on the developer's RTX 3090. Windows CI still has no Vulkan.
- Only an RTX 3090 is available: AMD and Intel behaviour, and the RDNA 2 worst case the plan names, are not verified.
- An own denoiser starts behind NRD in quality. It can be improved or replaced: ADR-0039 makes it a concrete class whose inputs and outputs are that seam, rather than an `IDenoiser` interface with a single implementation.
- Every 9.x step ends with a GPU-time measurement in the factory and a comparison with the path tracer.
- The plan's phase numbers (0 to 7) do not match the engine's; its Phase 0 to 5 are the steps above, its Phase 6 (neural) is out and its Phase 7 is the deferred list.

## Alternatives considered

- Keeping each raster technique as an option when its ray-traced replacement is off: two implementations per feature to maintain and test. The owner chose ray tracing only.
- NRD, SHaRC and RTXDI: mature and fast, but their licence conflicts with the engine's.
- The visibility buffer and mesh shader pipeline of the plan first: a large rewrite of a path that is not the bottleneck.
