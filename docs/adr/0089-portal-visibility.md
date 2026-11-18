# ADR-0089: Portal visibility

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 17.1 (ADR-0088). The dressed factory drew 1.8 million triangles from anywhere. Frustum culling (ADR-0087) kept 416 of 483 draws from the start room, because the view runs the length of the level behind its walls. ADR-0073 deferred portal visibility until content needed it. The owner asked whether it was implemented: it was not.

## Decision

- **Cells are boxes in the level file:** `"cell": {}` on an entity makes its box, from -0.5 to 0.5 scaled by its transform, a room or a doorway (`scene::VisibilityCell`). Cells are axis-aligned in the world, as the factory's rooms are.
- **Portals are found, not drawn:** where two cells' boxes overlap is the opening between them. Rooms with a wall between them do not overlap, so they do not see each other. A doorway is a small cell overlapping both rooms, or a corridor cell that reaches a little into each.
- **The walk** (`renderer::VisibleCells`):
  - It starts from the cells holding the camera.
  - From a seen cell, it goes through each portal whose corners project inside the part of the screen the way there left, and narrows that part to the portal's on-screen rectangle. A portal partly behind the camera leaves the whole screen; a portal wholly behind it is not seen.
  - With the camera in no cell, everything is seen.
- **What is skipped:**
  - Draws, decals and wet volumes whose bounding sphere touches no seen cell.
  - Draws are not rasterized. Decals and water are not tested against pixels, which turned out to be the larger saving.
  - What lies outside every cell is always drawn, so a room left without cells loses culling, never visibility.
  - Rays still meet every draw, in reflections, shadows and bounced light.
- **`scripts/make_visibility_cells.py`** makes the factory's ten cells: the main floor's rooms and corridors, the yard and the pit. It fills the free floor from a seed in each room, on the dressing's grid of the level's geometry, and takes the box around it, grown by a quarter of a meter so that openings overlap. The upper floor and the underground have no cells yet.
- **The editor** shows cells as boxes and can add them in the inspector.

## Measurements

At 1080p on High, RTX 3090:

| | Start room | Loading floor |
|---|---|---|
| Triangles drawn | 1.8 M → 53 k (21 draws) | 1.8 M → 583 k |
| Frame | 12.8 → 11.5 ms | 15.3 → 14.4 ms |

The start room's frame is back to its time before the dressing. The showcase's eight fixed shots show the same images before and after.

The G-buffer itself went only from 1.6 to 1.3 ms. Its cost was never mostly triangles: the per-pixel loops over decals and wet volumes cost more.

## Consequences

- A level needs cells to benefit. Authoring them is boxes over rooms, by hand in the editor or by script.
- A room in a cell whose opening was not given a doorway cell disappears from views through that opening. That is the risk of portals; the fixed shots are the check.
- Non-axis-aligned rooms get the box around them, which culls less.

## Alternatives considered

- **Occlusion against the previous frame's depth (Hi-Z) on the GPU:** no authoring and no risk of a missing doorway. But it needs draws made on the GPU (indirect, compacted), which the mesh pass does not do, and it would not have culled the decals and water that cost the most.
- **Precomputed visibility (PVS):** exact and free at run time, and a bake to redo after every edit of the level.
- **Software occlusion on the CPU** (rasterizing the walls at low resolution): no authoring, and a millisecond of CPU a frame.
