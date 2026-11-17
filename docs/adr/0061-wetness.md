# ADR-0061: Wetness, puddles and running water

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 6 (ADR-0041) asks for wetness: a porosity per material, wetness from vertex colors, textures and decals, darkening by porosity, a water coat (IOR 1.33), drying over time; puddles from a height and a water level, with ripples from drips, steps and impacts; running water on walls from flow maps; a flooded test room. The factory's rain room (step 9.24) had rain falling on a dry floor.

## Decision

- **Wetness lives in the material function,** `ApplyWetness` (`Wetness.slangh`), after decals, in the G-buffer and at ray hits, so reflections, glass, bounced light and the path tracer see the same water.
- **Wet volumes say where water is** (`renderer::WetVolume`, `scene::WetVolume`, `wet` in level files): a box with a wetness, a puddle coverage, the height above its bottom up to which water pools, a rain rate, and how much water runs down walls. Sixteen per frame.
- **A wet surface:** porous ones darken by up to 60% (water fills their pores), every one gets smoother and a film of water, a clear coat over the material (ADR-0056) of the water's roughness.
- **Puddles** form on what is flat (by the surface's own normal, not its normal map's bumps) below the water height, in the low spots of a noise whose threshold is the coverage. There the surface is flat, smooth, and darker below the water, and rippled.
- **Ripples:** rain drops at random places of 25 cm cells, at the volume's rate, each a ring that spreads and fades; and up to 16 ripples the game starts (`Scene::AddRipple`): the sandbox rings the water at each footstep and where shots land.
- **Running water on walls** is a streak noise that scrolls down with time, across the wall's own horizontal direction, rather than flow maps.
- **Porosity per material** comes from the glTF extras `{"porosity": 0.8}`, or from the material's surface when it has none (concrete 0.8, wood and cloth 0.7, tile 0.2, metal and glass 0.05).
- **Drying:** a volume loses `dryingRate` of wetness per minute, and its puddles shrink with it.
- **`renderer.debug_view wetness`** tints the lit scene blue as wet as surfaces are, and standing water cyan.
- **The factory's rain room is wet:** puddles on its floor, rippled by its rain, and water down its walls.

Measured on an RTX 3090 (`AWetFloorMatchesThePathTracer`): a floor with puddles is 0.006 FLIP from the path tracer, against 0.30 dry.

Making this step showed that the shader build rule lists every header a shader includes (`INCLUDES` in `engine/renderer/CMakeLists.txt`): a header left out, as `Decals.slangh` was in step 11.5, is not rebuilt when only it changes. Both new headers are listed now.

## Consequences

- Wetness from vertex colors and textures is not read: volumes carry it. A painted wetness map comes with the first asset that needs one.
- Wet decals (a stain of water) are not separate from volumes: a small wet volume does the same.
- Flow maps would steer water around a wall's details; the streaks fall straight down.
- The flooded test room is the rain room and the GPU test's floor, not a level of its own.
- Deep water, with refraction and caustics, stays deferred (ADR-0041).

## Alternatives considered

- **A screen-space puddle pass** (water drawn over the G-buffer): cheaper per pixel, but reflections and the path tracer would not see puddles, and ripples would not be lit by ray-traced lights.
- **Puddles from a painted height map per level:** exact control of where water stands, at the cost of painting; the noise in low spots of the real geometry gives plausible puddles for free, and a mask can come later.
