# ADR-0106: Seeing the level in the editor

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.2 of Phase 20 (ADR-0104): proposal items 11 to 14 and 16.

- **Everything was drawn at once.** Markers of every kind, boxes of every volume and the grid could not be turned off, and the factory's start room alone shows about 60 markers.
- **Overlays showed through walls** (deferred in ADR-0055). They are ImGui lines over the image, with no depth.
- **Only the selected light showed its reach.** A spot light's cone was not drawn at all.
- **No views of collision, navigation or rooms.** ReSTIR's cost depends on how many lights overlap (ADR-0044), and nothing showed it.
- **The scene's time always ran.** Flickering lamps and particles kept moving under what was being placed.

## Decision

- **A Show menu.**
  - Markers by category: lights, effects, fog/wind/water, triggers, rooms, decals, game, models, empty entities. Whatever is selected always shows.
  - Overlays: the grid (now Shift+G), every light's reach, collision boxes, the navigation mesh, the openings between rooms.
  - What the scene draws: decals, particles and ribbons, and fog. These are taken out of the frame in `OnRender`, as the editor light does (ADR-0105).
  - **Game view (G)**, as in Unreal Engine: no grid, markers, boxes or gizmo. Clicks still select.
  - `editor.show` turns overlays on at start (`collision,navigation,portals,light_reach,game_view`, and `no_grid`, `no_decals`, `no_particles`, `no_fog`).
- **Overlays drawn in the scene's depth, by a depth probe.**
  - With `Renderer::SetDepthProbe(true)`, a compute pass writes a grid of the G-buffer's depth in meters, at most 320 cells wide with the frame's shape. Each cell holds the farthest of four samples, so lines lying on a surface stay in front of it.
  - The grid is copied for the CPU and read back two frames later with the view-projection it was drawn with (`Renderer::LatestDepth`).
  - The editor's `DepthTest` projects a point with that matrix. A point behind the scene's depth by more than 2% and 5 cm is drawn at 22% of its alpha.
  - Lines are drawn in half-meter pieces, each faded by its middle; markers by their center.
  - What is hidden stays faint rather than gone, as in other editors, so a light behind a wall can still be found and picked.
- **Light reach.** A point light shows three circles of its range; a spot light shows its outer cone to its range, as a rim and four lines. These show for the selection, or for every light within reach with "Every light's reach".
- **New views.**
  - **Collision:** a body's box, cyan when static, magenta when it moves.
  - **Navigation:** the mesh's polygon edges (`NavMesh::Edges`). It is built when the view is turned on, from every model's triangles, as the game builds its own (ADR-0069). Doors are walls here, since only the game knows them.
  - **Openings:** where two room boxes overlap, the portals the renderer walks through (ADR-0089).
  - **Lights reaching each pixel** (`renderer.debug_view light_count`): ReSTIR's shading pass counts the analytic lights whose range covers each pixel's surface and shows the count as heat, from blue (one) to red (16 or more).
- **Real time (Ctrl+R)** pauses the scene (`Scene::SetPaused`): flickers, particles, animations and sequences hold still while things are placed.

## Consequences

- The depth probe costs one small dispatch and a 400 KB copy a frame in the editor. The game never turns it on.
- Overlays lag the view by two frames while flying, so faint parts trail by a few pixels and settle when the camera stops.
- The light-count view loops over every analytic light per pixel. It is meant for looking, not for timing.
- An audio-zone view is left for step 20.12, which brings sound into the editor.

## Alternatives considered

- **Lines drawn by the renderer, in the scene's depth:** exact, but it needs a line pipeline (the RHI has none), a vertex stream per frame and wide lines, which Vulkan does not guarantee. The probe needs one compute shader and gives the same reading for markers and text too.
- **A CPU ray per marker and line piece** against the picker's triangles: exact and current, but thousands of rays a frame for the grid.
- **Hiding what is behind walls:** cleaner, but a light behind a wall can then not be found from the room next to it.
