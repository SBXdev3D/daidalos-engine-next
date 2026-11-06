# ADR-0030: Developer tools: a console, statistics, an entity inspector and resources

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

Since Phase 3, development builds have had the Dear ImGui overlay (ADR-0015), but only the sandbox used it, for one window of frame rate, GPU times and help. Settings could only change through files and the command line, the scene could not be looked into while running, and text fields did not work, because the platform delivered no text. Phase 8 ends with the tools listed in OVERVIEW: entity inspection, rendering statistics and resource diagnostics, without gameplay code reaching into the backend.

## Decision

- **The tools belong to the `app` module, in development builds only** (`DAI_DEV_TOOLS`), as `app::DevTools`, owned by `Application` next to the overlay, and drawn after the frame's updates. Builds without them have neither the code nor ImGui.
- **The key under Escape opens and closes them** (`Key::Grave`, by position, whatever the layout says it is: ² on AZERTY). Open, they show a menu bar and their windows, free the pointer and take the keyboard and the mouse: the game's `InputState` gets no key, mouse or text events, and the keys it held are released. Closing captures the pointer again if it was. Closed, one line of statistics stays in the top-left corner.
- **Text input**: `platform::TextEntered` carries UTF-8 text from SDL, delivered while `Window::SetTextInput` is on, which the tools turn on only while they are open, because some systems show an input window then. The overlay passes it to ImGui.
- **The console** shows the log, through a sink that keeps the last 2,000 lines from any thread (`app::ConsoleLog`), and runs lines in a small language (`app::Console`, which has no ImGui and is tested on its own): a command and its arguments, a cvar's name to show it, or a name and a value to set it **for this run only**; the player's settings stay with the options (ADR-0029). Tab completes the first word, up and down walk the history. Built in: `help`, `cvars [filter]`, `clear`, `windows`, `quit`; games add their own with `Console::AddCommand`.
- **Statistics** show the frame times of the last 240 frames, the GPU time of each pass, what the renderer drew (`Renderer::Stats`: draws, triangles, lights, shadow draws, particle spawns), the scene's entity count, and the device's video memory and live objects (`rhi::Device::Memory`, from VMA's heap budgets).
- **The entity inspector** shows the current scene's entities as their hierarchy, named by `NodeName`, with a filter, and the components of the one picked. Components have no reflection: each type shown has an inspector, a function that draws it with ImGui and may edit it. The engine registers its own (transforms, camera, mesh renderers, light, particle emitter, kinematic body, sound emitter, name); a game adds its own with `DevTools::AddInspector<T>`. Entities cannot be created or destroyed from the tools, because physics bodies and sounds would be left behind.
- **Resources** lists the current scene's assets by path (`AssetLibrary::Describe`) and the renderer's and device's object counts.
- **Games add windows** with `DevTools::AddWindow`; the sandbox's old overlay window becomes its "Game" window, and its player gets an inspector and a `restart` command.
- **Tool windows are opaque.** The overlay blends in linear light, where ImGui's 94% opaque backgrounds let through what looks like a quarter of the scene, and bright pixels read as noise.

## Consequences

- Settings can be tried while playing, and the log can be read without leaving the game.
- A component type shows in the inspector only once someone writes its inspector; game components need a few lines each.
- The tools cost nothing in builds without them. In development builds, the overlay with every window open takes the GPU 0.04 to 0.08 ms at 720p; the CPU cost was not measured.
- The game keeps running behind the tools, to be inspected while it moves; a game pauses itself if it wants to.
- The console log is a copy of the log for the tools' lifetime; `sandbox.log` stays the complete record.

## Alternatives considered

- **Reflection for components** (a registry of fields, or a library such as refl-cpp): every field would show without code, but it needs registration anyway and a dependency, for a handful of engine components.
- **A console on the game's own canvas** (ADR-0027), in every build: players do not need one, and ImGui already does text fields, lists and tables.
- **A separate tools window or process**: the tools would survive a crash and leave the game's view alone, but they would need a protocol to see the scene.
- **Editing settings through `ChangeSetting`, saved like the options**: an experiment in the console would end up in `user.cfg` without the player asking.
