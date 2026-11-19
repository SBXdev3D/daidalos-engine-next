# ADR-0094: The material grid

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.2 (ADR-0092). Decals (ADR-0060) and wet volumes (ADR-0061) are applied in the material function, by the G-buffer and at every ray hit. A pixel tested every decal its draw's 64-bit mask named, and every wet volume of the frame, each a matrix product. A large draw, such as the factory's floor, touches most decals, so its pixels tested most of them. The dressed factory has 38 decals and 15 wet volumes, and impacts add more.

Measured on High at 1080p on an RTX 3090, turning each off:

| | G-buffer | Indirect light |
|---|---|---|
| Decals | 0.33 ms | 0.3 ms (ray hits) |
| Wet volumes | 0.14 to 0.33 ms | |

The plan said a grid of the screen, "as lights have". Lights no longer have one: ReSTIR replaced the clustered light lists (ADR-0044). A grid of the screen would also help only the G-buffer, while ray hits paid as much.

## Decision

- **A grid of the world around the camera,** 64 by 16 by 64 cells of 2 m: 128 m across, 32 m high. Each cell holds, a bit each, the decals and the wet volumes whose boxes touch it (`MaterialGrid.slangh`).
- **A compute pass writes it each frame** (`MaterialGrid.slang`, "Material grid" in the GPU times), before the G-buffer: a thread per cell tests the boxes around the decals and volumes, which the CPU writes with them. It takes 0.02 ms.
- **The grid's cells are whole cells of the world.** Its corner moves by whole cells with the camera, so a surface's cell does not change while the camera moves within one.
- **A point looks up its cell,** in the G-buffer and at ray hits alike. A decal is tested when both its draw's mask and the cell name it; a wet volume when the cell names it.
- **Outside the grid, every decal and volume is tested,** as before: far ray hits stay correct.
- **The frame names the grid only once the pass is added** (`MeshPass::AddMaterialGrid`). Until then, shaders test everything, so a pass that draws without the grid, such as the path tracer's tests, stays correct.

## Measurements

High at 1080p, RTX 3090, dressed factory, flashlight on. The old and new builds ran in turns, twice each:

| | Start room | Loading floor |
|---|---|---|
| G-buffer | 1.27 to 1.34 → 0.87 ms | 1.30 to 1.35 → 0.80 ms |
| Indirect light | 3.0 to 3.3 → 2.7 ms | 3.5 to 3.8 → 3.5 to 3.6 ms |
| Frame | 11.2 to 13.6 → 10.6 to 10.7 ms | 14.3 to 14.4 → 13.7 ms |

With the grid, the G-buffer costs less than it did with decals turned off (0.92 to 0.97 ms), since wet volumes are culled too.

**1 m cells** (64 m across) were no better: the G-buffer took 0.84 to 0.95 ms. More points fall outside a smaller grid and test everything.

The path tracer's tests now rasterize with the grid and trace without it, so they compare the two: decals on a floor and in reflections, and a wet floor with puddles, pass with their thresholds unchanged. The showcase's eight shots, 16 times brighter, and the decal and wetness debug views of the start room, the loading floor, the west room, the workshop and the yard show the same images before and after.

## Consequences

- A decal or wet volume is tested in every cell its box's bounding box touches. A long thin decal turned 45 degrees covers more cells than it needs; the per-pixel test stays exact.
- Up to 64 decals and 16 wet volumes, as before (ADR-0060, ADR-0061): the grid's bits match those limits. More would need wider cells.
- The grid holds 1 MB on the GPU.

## Alternatives considered

- **Tiles of the screen,** with a depth range per tile: the common way for deferred decals, but rays would gain nothing, and indirect light paid as much as the G-buffer.
- **Writing the grid on the CPU:** 1 MB to clear and upload a frame, or a sparse list to search. The GPU writes it in 0.02 ms.
- **A mask of wet volumes per draw,** as decals have: it would help both, but large draws touch most volumes, as they touch most decals.
