# ADR-0124: The light grid

- **Status:** Accepted
- **Date:** 2026-11-24
- **Amends:** ADR-0044 (candidates are drawn mostly from the point's cell, not by power alone), ADR-0094 (the light grid shares its cells)

## Context

The owner compared the effects gallery (ADR-0122) with the factory: the gallery's light looked cleaner, the factory's dark places full of speckle. Part of it was the factory's lighting, which a relight on the same branch addressed (wider legacy lamps, weak fill lights). The rest was sampling.

Every point that draws lights (ReSTIR DI's candidates, the bounce's and the radiance cache's hits, reflections, glass, particles) drew its candidates from one alias table of the whole frame, by power (ADR-0044). In the factory, the two 2000 cd wall lamps of the pump level's fan rooms are 50% of the power: every pixel of the level spent half its eight candidates on two lamps it cannot see. A 140 cd caged lamp is 1.75%: the pixels it lights found it in one frame out of eight, and the rest of the time showed the noise of what else they drew. Adding weak fill lights made it worse: they lit the walls, and they were the lights drawn least. The gallery hid the problem, since each room is lit by its strongest lamp, the one power favours.

## Decision

- **A grid of the world lists the sources that light each cell most.** It shares the material grid's cells (64 × 16 × 64 cells of 2 m around the camera, ADR-0094) and their origin. A compute pass ("Light grid" in the GPU times), after the emissive lights and before anything draws lights, gives each cell's thread every source of the frame, as the CPU writes them beside the alias table (`GridSource`): an analytic light by its luminance in candela, its range and its cone; a directional light by its lux, in every cell; an emissive draw by its flux over a hemisphere, from its bounding sphere. A source's weight is at most what it could send to a point of the cell, by the inverse square of the distance to the cell's center, no nearer than its half-diagonal, zero beyond the range or outside the cone from anywhere in the cell. Shadows are not known: the fan-room lamps still weigh in the corridor beside them, which they light through the fans.
- **A cell keeps its 16 heaviest sources**, sorted by index, each with the odds of drawing it or one before it in 16 bits: 64 bytes a cell, 4 MB.
- **Three candidates in four come from the cell, the rest from the table by power.** Each candidate's probability is what both give it, `0.75 × cell + 0.25 × power`: a source the cell left out, or weighed wrong, is still drawn, so the light stays unbiased whatever the weights (one-sample multiple importance sampling over the two strategies). A point outside the grid, or in a cell that lists nothing, draws by power alone.
- **A point looks up its cell from a place moved at random within a cell of it**, once per draw of candidates, so that neighbouring cells' lists blend rather than meet at a seam. Without a grid, nothing changes: no number is drawn for it, and passes that do not add the grid, such as tests, draw as before.
- **Points keep only the cell's address**, not its list: the list is read again for each candidate, from the cache, and a candidate drawn from the cell finds its odds on the way. The heavy ray-tracing passes keep their registers (see Measurements).
- **`renderer.light_grid`** (on by default) turns it off, to compare. The volumetric fog keeps its own draw (ADR-0051).

## Measurements

RTX 3090, 1920 × 1080 at full render scale, Ultra, the workshop of the relit factory, without the flashlight; each a separate launch, read from the developer tools' GPU times:

| | Grid off | Grid on |
|---|---|---|
| Light grid | | 0.05 to 0.06 ms |
| Radiance cache | 0.42 to 0.45 ms | 0.49 ms |
| Indirect light | 3.86 to 4.35 ms | 4.14 to 5.44 ms |
| Reflections | 0.52 to 0.61 ms | 0.58 to 0.87 ms |
| Direct lighting | 2.56 to 2.69 ms | 3.03 to 3.49 ms |
| Total | 11.72 to 12.27 ms | 13.26 to 14.56 ms |

The grid costs 1.3 to 2.6 ms there, and the same setting varies by up to a millisecond from one launch to the next. Its own pass is 0.06 ms, and looking up the cell without the random move costs the same. What costs is light that was skipped: drawn by power, most candidates were the fan rooms' lamps, 75 m away and beyond their 25 m range, which send nothing, so many points kept no light and cast no shadow ray, which is also why they were black or speckled. With the grid, nearly every point finds a lamp that lights it and traces the ray to it.

Holding the cell's 16 words in registers instead: one launch in the factory gave 14.16 ms in all (indirect light 5.13 ms, reflections 1.11 ms), within the spread above; in the room of a thousand lights at 1080p, where every lamp is in sight, ReSTIR DI took 2.99 ms with the list in registers, 3.43 ms reading it from the cache, 2.88 ms without the grid. The factory is what the grid is for, and its heaviest passes are the ray tracing ones, whose registers the cache leaves alone. The grid's pass takes 0.58 ms with a thousand lights: a thread per cell weighs every source.

`AWeakLampIsDrawnOverAStrongOneInAnotherRoom`: a room lit by a 50 cd lamp, and 30 m away a closed room with a 3000 cd one. One frame of ReSTIR DI against the path tracer's direct light: FLIP 0.46 to 0.47 by power, 0.043 with the grid; 128 frames averaged are as bright as the path tracer (0.9996). The room of a thousand lights is unchanged: one frame 0.352 (0.356 before), averaged 0.170. Every renderer GPU test passes.

The showcase's eight places, without the flashlight, grid off and on (`showcase_shots.ps1 -NoFlashlight`, `-Extra +renderer.light_grid,false`): the speckle on the walls and ceilings the fill lights and the caged lamps light, in the workshop, the offices, the west room and the hall, is mostly gone; lit areas look the same.

## Consequences

- Each point that draws lights reads its cell's 64 bytes once per candidate.
- The factory costs 1.3 to 2.6 ms more on an RTX 3090 at 1080p on Ultra: the light it now finds, it traces.
- A level whose light comes from many weak lamps, as the factory's, is less noisy, and weak fill lights become a tool level design can use.
- Above 16 sources that matter in a cell, the lightest of them come from the table by power, as every light did before.
- Shadows are not in the weights: a lamp behind a wall weighs as if the wall were not there. The power table, and ReSTIR's reuse, still sort it out, as they did before.
- The grid's pass weighs every source in every cell: 0.06 ms for the factory's hundred or so, 0.6 ms for a thousand.

## Alternatives considered

- **Lowering the fan-room lamps**: at 400 cd, the hall's speckle dropped, but the pump corridor they light through the fans went dark. It is the owner's lighting, and the next level would have its own strong lamp.
- **Clusters of the view** (clustered forward's lists): the bounce's, the reflections' and the particles' points are not on screen, and the material grid already gives cells of the world.
- **The whole list in registers**: faster in the room of a thousand lights (2.99 against 3.43 ms), no better in the factory, and more registers in the ray-tracing passes.
- **Light trees or BVHs of lights** (as in Conty Estevez and Kulla 2018): sampling by a tree of lights adapts to any number of lights and their extent, but costs a traversal per candidate and a tree each frame; a grid of a hundred lights was enough here.
- **Without the table by power**: a light the cell leaves out could never be drawn there, which would be biased.
