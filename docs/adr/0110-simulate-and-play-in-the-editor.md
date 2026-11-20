# ADR-0110: Simulate, and play in the editor

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.6 of Phase 20 (ADR-0104): proposal item 4, the largest gap the assessment named. F5 played the level in the sandbox as a separate process (ADR-0055): a second window, a second load of the factory, and nothing of the game inspected from the editor. Two smaller problems were found on the way:

- **The editor's scene ran physics.** Loose objects (ADR-0073) fell while a level was being edited, and the gizmo then wrote where they fell into the level.
- **The game ran only as an executable.** Its handler, `Sandbox`, lived in the sandbox's `main` file, registered its settings as if it started once, and quit the application to end.

## Decision

- **Physics holds still while editing** (`Scene::SetPhysicsPaused`). Effects, flickers and sequence previews still run, as before.
- **Simulate (Alt+S, or Play > Simulate)**, as Unreal Engine's Simulate:
  - physics runs, with effects and animations, in the editor's view;
  - the camera stands in for the player: it gets a `TriggerActivator`, so walking it into a trigger plays the trigger's scare;
  - edits wait, since what the view shows is where physics put things, not the level. `Change` refuses them with a message, the gizmo is hidden and the inspector is read-only. The inspector shows where the selection is in the simulation;
  - Ctrl+R pauses and F10 steps one frame (also outside Simulate);
  - Alt+S or Escape stops: the scene is made again from the level, as it was. K stops and keeps where the loose objects came to rest, as one step of the history (Unreal's "Keep Simulation Changes").
- **Play in the editor (Alt+P from the start, Alt+Shift+P from the camera).**
  - The game is a library, `sandbox_game`, behind `sandbox::Game::Create(GameOptions)`; the sandbox executable is its `main` alone, and the editor links the same library.
  - The editor writes the level as it is beside its file (as F5 does), sets the game's settings (that level, no menu, the camera as the start), puts its own scene aside alive (`Application::ExchangeScene`, ADR-0109), and runs the game's handler in its window. For a frame and its rendering, the editor forwards `OnUpdate`, `OnRender` and `OnEvent` to the game, which has the whole window, its HUD and its sound.
  - Shift+Escape stops (Escape opens the game's own menu, as in the game). Quitting from the game's menu stops the game, not the editor (`GameOptions::quit`). The game's scene goes, and the editor's comes back as it was left.
  - The game can start again in the same run: its settings are fetched with `CVarRegistry::Obtain`, which returns a setting already registered instead of registering it twice. Embedded, the game adds no developer windows and leaves the window's title alone.
- F5 and Ctrl+F5 still start the sandbox as its own process, for playing exactly as players do.

## Consequences

- The editor starts the audio device, for the game it plays. Its own scenes stay silent (step 20.12 brings sound into the editor).
- The game's settings live in the editor's run: a binding changed in the game's options while playing in the editor lasts until the editor quits, and is not written, as the editor writes no player settings.
- The game's first scene becomes current a frame after its start when started from inside the editor's loop, so the game now loads it through its own reference.

## Alternatives considered

- **Playing inside the editor's viewport, its panels around it:** the game's HUD is laid out for the whole window and its mouse is captured. Unreal's own option to play in a viewport keeps the HUD in it with a viewport-sized canvas, which the game's interface does not have.
- **Running the game in the editor's own scene, then putting the scene back:** no second load, but the game changes everything it touches (entities destroyed, doors opened, the player added), and undoing all that is the same as making the scene again. A second scene keeps the editor's untouched.
