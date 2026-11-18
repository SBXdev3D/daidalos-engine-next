# ADR-0069: Navigation meshes with Recast and Detour

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 13 (ADR-0041) asks for navigation meshes and pathfinding with Recast and Detour, with doors, vents and moving obstacles. The factory's zombies walked straight at what they chased, turned back when they got stuck against a wall, and waited where they last saw the player.

## Decision

- **A `navigation` module** wraps Recast and Detour (vcpkg `recastnavigation`, zlib licence), as `physics` wraps Jolt (ADR-0021): `navigation::NavMesh::Build` makes one mesh from triangles in world space (cells of 20 by 10 cm, a walker 1.8 m tall and 35 cm wide that climbs 45 cm and walks slopes up to 45 degrees), `FindPath` gives the corners of the shortest way, or of the way to the nearest reachable point, and `NearestPoint` finds the mesh near a point.
- **Doors are areas of their own:** `Build` takes the doors' boxes and marks them, so that Recast cuts polygons at their edges; `SetBlocked` shuts or opens the door polygons in a box, which path queries then avoid or take.
- **The scene holds the mesh** (`Scene::Navigation`), which the game builds once its level is in, as it gives its geometry to physics and sound.
- **The factory** builds its mesh from its level geometry and its doors, 231 polygons in 30 ms; doors shut the mesh while they are closed and open it as they rise. A zombie with somewhere to go asks for a path, again every half second or when its goal moves a meter, and walks to its next corner.

Tested on a floor with a wall and a doorway: a path from one side to the other goes through the doorway (`APathGoesThroughTheDoorway`), stops on the near side while the door is shut and goes through once it opens (`AShutDoorBlocksTheWay`).

## Consequences

- Vents and other ways only some walkers take are not in the mesh: they would be off-mesh links or areas a filter allows.
- Moving obstacles other than doors (a pushed crate) do not change the mesh; Detour's tile cache would carve them, with a tiled mesh.
- The mesh is one tile, built at each start of the level; a level much bigger than the factory would want tiles built on worker threads and saved with it.

## Alternatives considered

- **A grid and A\* of our own:** simple, but a grid fine enough for doorways is large, and paths along it zigzag; Recast's polygons and Detour's string pulling give few, straight corners.
- **Steering around obstacles only:** what the zombies did; it fails at every corner the factory has.
