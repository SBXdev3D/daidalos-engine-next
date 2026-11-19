# ADR-0102: The ray shaders measured; the driver's statistics on request

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 19.3, the last of Phase 19 (ADR-0099): make the ray-traced shaders themselves faster without changing the image, from a profile. Nsight Graphics is not installed on the development machine, and downloading it needs an NVIDIA developer account; the plan allowed timing variants instead. What the driver can say without it: `VK_KHR_pipeline_executable_properties`, which the RTX 3090's driver supports, reports each pipeline's registers, local memory and code size.

## Decision

- **`renderer.log_pipeline_statistics`**, read at startup (`rhi::DeviceConfig::pipelineStatistics`): compute pipelines are created with `VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR`, and the driver's statistics of each are logged. Ignored on a driver without the extension.
- **No shader is changed:** none of the variants measured below makes the frame faster without changing the image.

## Measurements

At 1080p on an RTX 3090, dressed factory, High, loading floor.

**Each pass of the ray-traced groups,** timed alone:

| Pass | Time | Registers | Spilled to local memory |
|---|---|---|---|
| Indirect light | 3.11 ms | 72 | 32 bytes |
| Reflections | 1.46 ms | 80 | 32 bytes |
| ReSTIR spatial reuse | 1.00 ms | 96 | none |
| ReSTIR candidates | 0.80 ms | 80 | none |
| Lighting (shading) | 0.48 ms | 80 | none |
| Radiance cache update | 0.43 ms | 72 | 128 bytes |
| ReSTIR temporal reuse | 0.25 ms | 80 | none |
| Reflections fill | 0.15 ms | 71 | none |

An Ampere multiprocessor holds 64K registers, so 72 to 96 registers per thread run 21 to 28 warps of its 48: occupancy of 44 to 58%, ordinary for shaders that trace rays and shade what they hit.

**ReSTIR's spatial reuse costs more than its candidates.** Per pixel, it evaluates a light at a surface ten times: once for its own sample, three times per neighbour. Four of those repeat values the earlier passes computed this frame and could be stored. Replacing them with a constant, which breaks the image, to bound what storing them could save: 1.11 ms against 1.00, no faster. The pass is bound by reading its neighbours, the G-buffer and reservoirs of three pixels up to 30 pixels away, not by the lights. Fewer or nearer neighbours would make it cheaper, and change the image.

**The radiance cache update's spill** is its path of three vertices, kept in arrays walked backwards. Unrolling them would trade the spill for registers on a 0.43 ms pass; not tried.

**Phase 19 in all,** the end of Phase 18 (ADR-0098) against now, two runs each:

| Preset | Start room | Loading floor |
|---|---|---|
| Low | 3.21 to 3.26 → 3.10 to 3.21 ms | 3.73 to 3.75 → 3.66 to 3.71 ms |
| Medium | 5.00 to 5.01 → 4.77 to 4.84 ms | 5.67 to 5.70 → 5.50 to 5.55 ms |
| High | 12.31 to 12.61 → 10.99 to 11.79 ms | 11.89 to 11.97 → 11.57 to 11.72 ms |
| Ultra | 12.96 to 13.08 → 13.54 to 14.43 ms | 13.83 to 14.35 → 13.24 to 13.39 ms |

The start room on High and Ultra switches between two states about 1.5 ms apart with either build (ADR-0097); the loading floor is the steady comparison: 0.1 to 0.6 ms less on every preset. The presets' shares of High stay within the runs' spread: the first launch's table (ADR-0098) is unchanged.

## Consequences

- The ray-traced frame is now bound by traversal, shading at hits and scattered reads, not by any waste that can be removed while keeping the image. What is left trades image for time: fewer ReSTIR neighbours, lobe-widened ray cones (ADR-0101), lower-resolution passes, which the presets already use.
- With Nsight Graphics, the next look would be at the indirect light's 3.1 ms: how much is traversal against shading at hits.

## Alternatives considered

- **Thread groups of another shape:** an 8 × 8 group's warps are already 8 × 4 tiles of the screen, as coherent as a warp can be; not measured.
- **Shorter rays for indirect light:** the factory is indoors; rays end at walls long before the 1e6 m limit, so traversal would not change.
