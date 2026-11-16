# ADR-0055: The level editor

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 10 builds a level editor (ADR-0041) on the level files of ADR-0054. The owner wants it to grow into a professional tool. It needs a viewport that is the game's renderer, panels around it, selection, gizmos, an inspector, and later undo, assets and play-in-editor (steps 10.3 to 10.5).

## Decision

- **An executable of its own, `apps/editor`, in development builds** (`DAI_ENABLE_DEV_TOOLS`), on `app::Application` like the sandbox. It opens `editor.level` (default `Factory/Factory.level.json`), runs no game code, and is silent: lights, effects, fog and animations play as they do in the game.
- **Dear ImGui with docking, and ImGuizmo**, through vcpkg (`imgui[docking-experimental]`, `imguizmo`, both MIT). The interface is the debug overlay's (ADR-0015), so one interface toolkit serves the engine's tools and the editor. The layout is saved in `editor_layout.ini` next to the executable.
- **The viewport is the dockspace's central node, and the renderer draws the scene into it.** `SceneView::viewport` is a rectangle of the window in pixels: the scene's targets take its size, the output pass places the scene there and leaves the rest black, and the interface layer still fills the window. The projection's aspect is the rectangle's, so gizmos, picking and markers line up with what is drawn, and no pixels are shaded under the panels.
- **Picking on the CPU.** A click casts a ray against the triangles that models already keep for physics, in each drawn entity's space behind a box per mesh, and selects the level entity the hit belongs to (`LevelInstance::IdOf`). Entities without a mesh (lights, emitters, fog, game markers) are drawn as markers and picked within 12 pixels of them. The factory answers in well under a millisecond, with no GPU readback and no ID buffer.
- **The document is the `scene::Level`.** Every edit goes through one function: a change of transform moves the entity; any other change makes it again with `LevelInstance::Create`. Undo and redo (10.3) record the entity before and after at that point.
- **Controls follow common editors.** Holding the right mouse button flies the camera with WASD, Q and E (Shift faster, the wheel sets the speed). Without it, the wheel walks and the middle button pans. W, E and R choose move, rotate and scale. F frames the selection, Escape clears it, and Ctrl+S saves.
- **`+editor.resave true`** reads a level, writes it in the engine's own layout and quits, after a script has written one.

## Consequences

- The engine's summary bar can be hidden (`DevTools::SetSummaryShown`); the editor shows its numbers in its status panel.
- The renderer's scene targets follow the viewport's size, so dragging a splitter makes them again each frame it changes, as resizing the window does. Editors resize rarely.
- Picking ignores skinned poses (the bind pose is used) and particles. Both are acceptable for placing level entities.

## Alternatives considered

- **Rendering into a texture shown with `ImGui::Image`** (a "Viewport" window): the usual layout, and the viewport could float. It needs the renderer to output to a texture and the overlay to sample it, a copy and a second output path. The central node gives the same layout with one rectangle.
- **The scene under the whole window, panels over it**: no renderer change, but a projection centered on the window and not on what is visible, and pixels shaded for nothing.
- **An ID buffer read back from the GPU for picking**: exact for every pixel, including skinned meshes and alpha, but a readback a frame late and a pass to maintain. Revisit if CPU picking falls short on larger levels.
- **Qt**: native widgets, but a second UI system, an LGPL dependency and a Vulkan surface hosted in Qt (asked and declined on 2026-11-16).
