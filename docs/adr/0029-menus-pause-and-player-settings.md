# ADR-0029: Menus, pausing and the player's settings

- **Status:** Accepted
- **Date:** 2026-11-06

## Context

After 8.1, the demo had a HUD but nothing around it: it started straight into the level, Escape quit at once, death left a message telling the player to press F5, and settings could only be changed by editing `daidalos.cfg` (ADR-0026). Phase 8.2 brings the menus of docs/DEMO.md: a loading screen, a main menu, a pause menu, options, and screens for death and for the end of the level. They need widgets that react to the mouse and the keyboard, a way to stop the game behind a menu, and a place to keep what the player changes.

## Decision

- **Widgets are immediate mode, like the canvas** (ADR-0027): `ui::Context`, in the `renderer` module next to `ui::Canvas`, has `Button`, `Toggle`, `Slider` and `Choice`. Each frame the game calls `Begin`, then one function per widget, which draws it and returns whether the player used it; the game keeps the values. The context only remembers which widget has the focus, by its place in the frame's calls, so a screen draws the same widgets in the same order every frame. `ResetFocus` starts a new screen and ignores the rest of the frame's input, so the click that changed screens does not also press a button of the new one.
- **Mouse and keyboard.** The widget under the pointer takes the focus when the pointer moves, and a left click uses it; sliders follow the pointer while the button is held. Up and down move the focus, wrapping around; Enter or Space use it; left and right change values. The pointer is placed on the canvas from window coordinates, the units of mouse events. Gamepads can drive the game, not yet the menus.
- **Values move only with input.** A slider never rounds a value it was given to its steps, or opening the options would count as a change.
- **`scene::Scene::SetPaused`** stops a scene's time: its fixed and frame systems, physics, animations, particles and sounds (`audio::SoundWorld::SetPaused`, which holds every voice where it is). The scene is still drawn as it was, so a menu shows the frozen level behind it.
- **The player's settings go to `user.cfg`, next to the executable**, read after `daidalos.cfg` and the game's file, and before the command line, which still wins. `Application::ChangeSetting` sets a cvar and remembers it as the player's; `Application::SaveSettings` writes those remembered settings: the ones `user.cfg` held at the start, and the ones changed since. Values that only came from the command line are not saved by accident. The file is written next to itself and then renamed over the old one, so a crash cannot leave half of it. `core::ParseConfigLines` and `core::FormatConfigLine` read and write the lines for it.
- **Volumes follow their settings while the game runs**, through `audio::AudioEngine::SetVolumes`, like the renderer's settings: the options' sliders are heard at once.
- **The sandbox's screens** are its own code, in `Menus.cpp`. Escape, or a gamepad's Start or Back, pauses the game and goes back from the options. The first level loads behind the legacy loading screen ("LOADING...", the author's turning gear in the corner, no progress bar), then waits paused behind the main menu. Two seconds after the player dies, or after the last zombie dies, the game stops on a death screen or a "factory cleared" screen with the time taken. A closed menu lets the game go on only once the mouse button is released, because the guns fire for as long as it is held. `sandbox.menu false` starts playing at once, for tests and screenshots.
- **The options** are the engine settings a player expects to change while playing: fullscreen, vertical sync, antialiasing, ambient occlusion, brightness (`renderer.exposure`), field of view, the three volumes and mouse sensitivity. Settings read only at startup, such as shadow resolution, are left to the file.

## Consequences

- A settings change is visible at once, and saved when the player leaves the options.
- The engine knows nothing about screens or menus: they belong to the game, which builds them from the context's widgets.
- Widgets have no identity but their order: a screen whose widgets come and go would need ids. None of the demo's do.
- Held keys do not repeat in menus, because `InputState` ignores repeats: a slider moves one step per press.
- Menus cannot be used with a gamepad yet.
- The menus draw in under 0.1 ms on the GPU at 720p.

## Alternatives considered

- **Retained widgets**: a tree of objects to keep in step with the game, for screens of a few buttons.
- **Saving every cvar that differs from its default**: it would also save values given on the command line, and later changes to the defaults would never reach players who saved once.
- **A separate settings file per game**: the engine's settings are the ones a player changes; one file next to the executable is simpler to find and to delete.
- **Pausing by not calling the scene from the application**: sounds and particles would go on, and every game would have to know which parts to stop.
- **A progress bar while loading**: a level is one glTF file, loaded at once on a worker, so a bar would have nothing true to show.
