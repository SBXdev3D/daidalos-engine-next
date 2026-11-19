# ADR-0092: Phase 18, performance

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

The assessment (docs/ASSESSMENT.md, ADR-0088) ranked what would make the frame faster, by expected gain on the measured frame: High on the loading floor, 15 ms at 1080p on an RTX 3090. On 2026-11-19, the owner asked to start the performance work.

## Decision

Phase 18 follows the assessment's list, one PR per step. Each step measures before and after, and compares screenshots at the game's brightness and 16 times brighter:

1. **18.1, indirect light at fewer pixels.** Half the pixels trace, or a quarter, in turns; the others are filled. ReSTIR's candidates per pixel are measured against noise.
2. **18.2, clustered decals and wet volumes.** A grid of the screen, as lights have, lists what touches each tile, so that a pixel loops only over those.
3. **18.3, async compute.** Passes that do not depend on the G-buffer, such as the radiance cache update, the fog's froxels and particle simulation, run on a compute queue beside the graphics queue.
4. **18.4, reflections.** Half resolution on rough surfaces, and reuse between frames.
5. **18.5, the denoiser.** Fewer spatial passes where a pixel's history is long and its noise low.
6. **18.6, GPU-driven rendering.** Draws are compacted on the GPU, with two-phase occlusion against the previous frame's depth (Hi-Z).

The CPU work in the assessment (a job system, multithreaded recording) waits until Tracy shows the main thread over budget. In the factory, the CPU is not the limit.

## Consequences

- A step that does not pay for itself in the measurements is recorded and left out, as levels of detail were (ADR-0090).
- The presets' relative costs (ADR-0052) are measured again at the end of the phase, for the first launch's choice.
