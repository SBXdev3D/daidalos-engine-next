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
- **The document is the `scene::Level`.** Every edit goes through one function, which changes the level and calls `LevelInstance::Sync(before, after)`: an entity whose transform alone changed moves, one that changed otherwise is made again (with the emitters other entities put on its surface), the ones gone are destroyed with what they put on other surfaces, and a changed environment is applied.
- **Undo keeps whole levels, not commands (10.3).** The history holds the level as it was before each step, 200 steps at most. An edit opens when something first changes and closes when the mouse lets go of the widget or gizmo, so a drag or a typed name is one step. Undo and redo are `Sync` between two levels, which no edit can get wrong: a command per field would have to be written, and kept right, for every field a later phase adds. The factory's level is about 30 KB of data, so 200 copies stay small. The history's revision of the last save tells whether the level has changed (the title's `*`).
- **Files (10.3).** Open lists the `*.level.json` files under `assets/`; New and Save As take a path under it, and Save As rewrites the level's relative paths (`scene::Relocate`) so they name the same files from the new directory. Dialogs are the editor's own ImGui modals, not the system's, so they look and behave the same on every platform and list only levels. Closing the window, opening or reloading with unsaved changes asks first (`IApplicationHandler::OnCloseRequested`).
- **Autosave (10.3).** A changed level is written every `editor.autosave` seconds (120 by default) to `autosave/` beside the executable, never over the level file. When a level opens and its autosave is newer and different, the editor offers it back as one undoable step; saving deletes it.
- **Play in the sandbox, as its own process (10.3).** F5 writes the level as it is to `<name>.play.level.json` beside it (ignored by git), so its paths still resolve, and starts the sandbox on it with no menu; Ctrl+F5 starts at the editor's camera (`sandbox.start`). The game runs as it ships, with its own window, input and state, and a crash in it costs the editor nothing. The copy is deleted when the sandbox exits.
- **Controls follow common editors.** Holding the right mouse button flies the camera with WASD, Q and E (Shift faster, the wheel sets the speed). Without it, the wheel walks and the middle button pans. W, E and R choose move, rotate and scale. F frames the selection, Escape clears it, and Ctrl+S saves.
- **`+editor.resave true`** reads a level, writes it in the engine's own layout and quits, after a script has written one.

## Consequences

- The engine's summary bar can be hidden (`DevTools::SetSummaryShown`); the editor shows its numbers in its status panel.
- The renderer's scene targets follow the viewport's size, so dragging a splitter makes them again each frame it changes, as resizing the window does. Editors resize rarely.
- `LevelInstance` builds in an order that creates parents and the models emitters sit on first, whatever the file's order, and links ribbons and lightning once per build instead of once per entity.
- Play is not inside the editor's window: the game's code would have to run in the editor's scene and be reset after. A separate process is simpler and closer to what players get; playing inside the viewport can come later if switching windows proves slow.
- Picking ignores skinned poses (the bind pose is used) and particles. Both are acceptable for placing level entities.

## Alternatives considered

- **Rendering into a texture shown with `ImGui::Image`** (a "Viewport" window): the usual layout, and the viewport could float. It needs the renderer to output to a texture and the overlay to sample it, a copy and a second output path. The central node gives the same layout with one rectangle.
- **The scene under the whole window, panels over it**: no renderer change, but a projection centered on the window and not on what is visible, and pixels shaded for nothing.
- **An ID buffer read back from the GPU for picking**: exact for every pixel, including skinned meshes and alpha, but a readback a frame late and a pass to maintain. Revisit if CPU picking falls short on larger levels.
- **Qt**: native widgets, but a second UI system, an LGPL dependency and a Vulkan surface hosted in Qt (asked and declined on 2026-11-16).
