# ADR-0105: A loader while a level opens; lighting modes in the editor

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.1 of Phase 20 (ADR-0104), both asked for by the owner.

- **Opening a level froze the editor.** `Editor::Open` loaded every model on the main thread before the next frame. The factory's 52 models take a few seconds, with the window not answering and nothing saying why. The sandbox already loaded them on workers behind its loading screen, with its own counting code.
- **The viewport had only the game's lighting.** The factory is dark on purpose, which makes placing props hard, and judging a light means looking past the textures. The renderer had 14 debug views, but only behind a cvar, and none showed surfaces unlit.

## Decision

- **`scene::ModelBatch`** loads a list of models on workers and calls back once, on the main thread, when the last is in. It counts what has arrived, keeps the failures, and can be cancelled. The sandbox's loading and the editor's both use it, so one piece of code counts models, and it is tested without a GPU through its loader function.
- **The editor opens levels in the background.** `Open` reads the level file (fast, and its errors are returned at once), makes the new scene and starts its batch; the level open stays in the view. A modal window shows the level's name, a bar with "51 of 52 models", the model that came in last and the seconds so far, with Cancel (or Escape). Editing, flying and shortcuts wait while it shows. When the batch finishes, the next frame builds the entities and swaps the scenes (`FinishOpen`). Cancel drops the new scene: the models still loading finish on the workers and go with it. Every path that opens a level goes through it: the first level, Open, New, Reload, Save As and a prefab opened from the inspector.
- **Four lighting modes**, in View > Lighting, in the toolbar, and on Alt+1 to Alt+4:
  - **Lit:** the game's image.
  - **Unlit:** a new `DebugView::BaseColor` that shows the G-buffer's base color, decals and water included, through the existing G-buffer debug pass. Nothing else is traced.
  - **Lighting only:** a new `DebugView::LightingOnly`, a flag in `FrameData::debugFlags` (4) that makes every surface a mid grey without metal where its material is read. That is in the G-buffer and at ray hits, so bounced light and reflections are grey too. The light, shadows and normal maps stay.
  - **Editor light:** the level's lights are taken out of the frame, and one soft point light rides just above the camera, like a miner's lamp. Emissive surfaces still glow. The eye's adaptation makes the room readable. Fog is taken out too, as the lamp scattered in it made a veil in front of everything. This is the editor's own state, never saved in the level.
- **The debug views are in a menu** (View > Lighting > Debug views, and the toolbar's lighting menu), named in words; a second click returns to the lit scene. The editor sets `renderer.debug_view`, so a view chosen in the console shows in the menu too.

## Consequences

- `renderer.debug_view` takes `base_color` and `lighting_only`, in the game too.
- Lighting only uses 0.5 for every surface, close to the average of the factory's materials, so its brightness reads like the lit scene's. A white surface would be about twice as bright and change the exposure.
- The loader's window shows only when loading takes a frame or more. Models the library already holds answer at once.

## Alternatives considered

- **Loading on a thread with the old scene destroyed first:** simpler, but the view would be empty while loading, and Cancel would leave nothing open.
- **Editor light as a light added to the level:** it would be saved, show in the hierarchy and follow nothing. A frame-only light costs nothing to remove.
- **Lighting only with white surfaces,** as some engines do: brighter than the scene, so the exposure moves and the light is harder to compare with Lit.
