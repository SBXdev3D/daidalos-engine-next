# Daidalos Engine Next

A modular C++20 real-time 3D engine for indoor environments, rendering with Vulkan 1.4 and hardware ray tracing on Windows and Linux.

The project is at an early stage. Phases 0 to 5 of the roadmap in [OVERVIEW.md](OVERVIEW.md) are done:

| Module | Contents |
|---|---|
| `core` | `Result` and `Error`, `DAI_ASSERT`, `DAI_TRY`, generational handles, frame timer, worker pool, configuration variables (cvars), profiling macros, logging over spdlog, and GLM with the engine's conventions: axes, TRS transforms, reverse-Z projections, bounding boxes |
| `platform` | SDL3 window, window and input events, Vulkan surfaces, process execution, crash dumps and their reports (Windows), and keyboard, mouse and gamepad state with actions |
| `resources` | Asset paths and caching, glTF models, PNG, JPEG and KTX2 images, as plain CPU data |
| `rhi` | Vulkan 1.4 device with ray queries, swapchain, buffers, textures with mip levels and BC compression, pipelines, command lists, batched uploads, deferred destruction |
| `renderer` | HDR rendering with PBR materials, normal maps, point, spot and directional lights and emissive surfaces sampled by ReSTIR DI over a G-buffer, ray-traced shadows, indirect light and reflections, glass, a sky, a reference path tracer, a denoiser, temporal antialiasing, particles simulated on the GPU, auto-exposure, bloom, AgX and HDR10 output, skinning, GPU timings, shader hot reload, and the Dear ImGui overlay in development builds |
| `physics` | Jolt behind a small API: static meshes, kinematic boxes, raycasts, character movement |
| `navigation` | Recast and Detour behind a small API: a navigation mesh from the level, paths, doors that shut it |
| `audio` | miniaudio and Steam Audio behind a small API: clips, voices, 3D sounds heard through an HRTF and through walls and doors, buses, silent without an output |
| `scene` | Entities with sparse-set components and a hierarchy, skeletal animation, the asset library, and the scene that ties them to the renderer, physics and sounds |
| `app` | Startup and shutdown in order, main loop, current scene |

The sandbox is a first-person demo in a dark factory, the remake of the legacy engine's demo: see [docs/DEMO.md](docs/DEMO.md).

## Documentation

- [OVERVIEW.md](OVERVIEW.md): vision, architecture, roadmap.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the engine is built today, with diagrams.
- [docs/ASSESSMENT.md](docs/ASSESSMENT.md): where the engine, the demo and the editor stand at the end of the roadmap, and what to do next.
- [docs/GUIDE.md](docs/GUIDE.md): how a game is made on the engine, with `samples/level-viewer` as the smallest one.
- [docs/DEMO.md](docs/DEMO.md): the demo that the sandbox is becoming, and its plan.
- [docs/CONVENTIONS.md](docs/CONVENTIONS.md): naming, ownership, error handling, math and rendering conventions.
- [docs/adr/](docs/adr/README.md): architecture decision records.

## Requirements

| | Windows | Linux |
|---|---|---|
| Compiler | Visual Studio 2022 (MSVC 19.4x) with the "Desktop development with C++" workload | Clang 18 or later |
| Build tools | CMake 3.25+ | CMake 3.25+, Ninja, pkg-config |
| System packages | | X11 and Wayland development packages, for SDL3 (see below) |
| Dependencies | vcpkg | vcpkg |
| To run | A Vulkan 1.4 driver and a GPU with hardware ray tracing (NVIDIA RTX 30xx class or newer) | The same (a recent `mesa-vulkan-drivers` provides lavapipe, with ray queries, without a GPU) |
| Optional | [Vulkan SDK](https://vulkan.lunarg.com/), for the validation layers | `vulkan-validationlayers` |
| Formatting | clang-format 19 (bundled with Visual Studio's "C++ Clang tools" component) | clang-format 19 |

Dependencies (SDL3, spdlog, GoogleTest, tl-expected, Vulkan headers, volk, the Vulkan Memory Allocator, Slang, Dear ImGui, fastgltf, libktx, stb, and Tracy for the profile presets) are installed automatically by vcpkg in manifest mode during the CMake configure step. They are listed in `vcpkg.json`, and their versions are pinned by the registry baseline in `vcpkg-configuration.json`. The GPU tests also use NVIDIA's FLIP (BSD 3-Clause), whose single header is in `third_party/flip`. The renderer includes AMD's FSR 3.1 shaders (MIT), in `third_party/fidelityfx-fsr3`.

### vcpkg

The presets need the `VCPKG_ROOT` environment variable:

- **Visual Studio's bundled vcpkg** (Windows): it is set automatically in a Visual Studio Developer prompt and inside the IDE. In another shell, set it to `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg`.
- **Standalone vcpkg**: `git clone https://github.com/microsoft/vcpkg`, run `bootstrap-vcpkg`, and set `VCPKG_ROOT` to the clone.

## Building

The build uses CMake presets. Build output goes to `build/<preset>/`.

### Windows

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug      # or windows-msvc-release
ctest --preset windows-msvc-debug
```

Visual Studio can also open the folder directly (File > Open > Folder) and use the presets.

### Linux

vcpkg builds SDL3 against the system's X11 and Wayland libraries. On Ubuntu 24.04:

```bash
sudo apt-get install ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev   libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev   libgl1-mesa-dev libdrm-dev libgbm-dev
```

Then:

```bash
cmake --preset linux-clang-debug               # or linux-clang-release
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

`scripts/validate_linux.sh` does all of it on Ubuntu 24.04 or under WSL2, with the scene smoke tests and the release archive, and writes a report to `build/linux-validation.txt` ([ADR-0074](docs/adr/0074-linux-validation.md)). From Windows, `scripts/check_linux_compile.py` parses every source as Clang does on Linux, against Ubuntu's headers.

The scene smoke tests (`ctest -L smoke`) play each scene of the sandbox for 150 frames with the validation layers on; `ctest -LE smoke` leaves them out.

### Sanitizers

`windows-msvc-asan` (AddressSanitizer) and `linux-clang-asan` (AddressSanitizer and UndefinedBehaviorSanitizer) are available with the same three commands.

### Profiling

`windows-msvc-profile` and `linux-clang-profile` build with optimizations and the [Tracy](https://github.com/wolfpld/tracy) client, used through the `DAI_PROFILE_*` macros in `daidalos/core/Profile.hpp`. The client records nothing until the Tracy profiler (version 0.14) connects to the running application. The profiler version must match the client: 0.14.1, set by the vcpkg baseline.

### Running the sandbox

```bash
build/windows-msvc/apps/sandbox/Debug/sandbox.exe   # Windows
build/linux-clang-debug/apps/sandbox/sandbox        # Linux
```

It finds `assets/` from anywhere in the repository, so it also starts with a double-click. The level is committed, and [docs/DEMO.md](docs/DEMO.md) explains how to play.

### Running the level editor

```bash
build/windows-msvc/apps/editor/Release/editor.exe   # Windows, development builds only
build/linux-clang-debug/apps/editor/editor          # Linux
```

It opens `Factory/Factory.level.json`, or the level given with `+editor.level <path under assets/>` (ADR-0055). Hold the right mouse button to fly with WASD, Q and E; click to select; W, E and R choose the move, rotate and scale gizmos; F frames the selection; Delete removes it; Ctrl+Z and Ctrl+Y undo and redo; Ctrl+N, Ctrl+O, Ctrl+S and Ctrl+Shift+S make, open, save and save a level under another name. F5 plays the level in the sandbox as it is, saved or not, from its start; Ctrl+F5 from the editor's camera. A changed level is autosaved beside the editor every two minutes (`+editor.autosave <seconds>`, 0 for never) and offered back if the editor stopped before it was saved. The Assets panel lists the models, skies, effects, prefabs, decal images and levels under `assets/`: double-click a tile to place it in front of the camera, or drag it onto what the view shows. Edit > Make prefab saves the selection as a `.prefab.json` file that other levels place, and puts it back as one entity; the inspector opens or unpacks the prefab an entity places (ADR-0078). B, or Paint in the toolbar, paints decals or wet patches: drag over surfaces in the view, one undo step per stroke. Click a surface and the Materials tab shows its material: changes show at once, Save into the model writes them into its glTF file. A trigger's "Edit its sequence" opens its scare as a timeline of cues to add, drag, change, play in the view and save; `+editor.sequence <path>` opens one at start (ADR-0079). Create > Trigger places a box that plays a `.sequence.json` scare when the player walks in; Create > Wet area makes a place wet, with puddles; Create > Decals places blood, grime, mold and scorch marks; Create > Light presets adds candles, buzzing tubes, failing lamps, emergency lights and more (ADR-0057). Levels open in the background with a progress window and Cancel. View > Lighting, the toolbar's lighting menu or Alt+1 to Alt+4 show the level lit, unlit, as light alone on grey surfaces, or lit only by a lamp at the camera, and list the renderer's debug views (ADR-0105). The Show menu turns markers on and off by kind, shows every light's reach, collision boxes, the navigation mesh and the openings between rooms, and leaves decals, particles or fog out of the view; G shows only the game's image, Shift+G the grid, Ctrl+R holds the scene still; what lies behind walls is drawn faint (ADR-0106). H hides the selection, Shift+H shows it alone, Alt+H shows everything, L locks it against clicks in the view; folders in the hierarchy take entities dropped on them; the toolbar's Pivot turns a selection around its origin, center or bottom; Edit > Align and Spread evenly line it up; M measures between two surfaces (ADR-0107). Shift+1 to Shift+4 show the level in perspective, from the top, the front or the side, cut through ceilings and walls ([ and ] move the cut); Shift+5 shows four views; View > Snap to surfaces puts what is moved onto what is under the mouse (or hold Ctrl+Shift), V snaps to corners, End drops the selection to the floor (ADR-0108). Ctrl+Z undoes material and sequence edits too, and the History panel goes back to any step; Ctrl+Shift+O opens a level or prefab in a new tab, Ctrl+Tab and Ctrl+W go to the next tab and close one (ADR-0109). Alt+P plays the level in the editor's window (Alt+Shift+P from the camera, Shift+Esc stops); Alt+S simulates it, physics and triggers running with the camera as the player, and K keeps where loose objects fell (ADR-0110). Ctrl+Shift+C checks the level for missing files, broken links, triggers whose sequences name missing entities, stacked copies and lights inside walls, a click away in the Map check panel; the Statistics panel shows the frame, the level and memory against their budgets (ADR-0111). Ctrl+P searches commands, entities, assets and settings; a right click in the view or the hierarchy shows what can be done there; F1 lists every key and changes any of them (ADR-0112). A placed prefab's parts can be changed for that copy alone in the inspector, or clicked in the view to open them; Apply to the prefab writes the changes for every copy (ADR-0113). Right-click an effect in Assets, or use an emitter's "Edit its effect", to edit it in the Effect panel, curves and gradients included; every emitter shows the change at once (ADR-0114). Create > Block out places boxes, walls, floors, stairs, ramps and pillars to try a room before modelling it; the game walks on them (ADR-0115). Drop a .wav or .ogg in the view to place an ambient sound, Show > Hear the level to hear it; the paint tool's Props brush scatters the selection's models; right-click Look through it, or the Camera preview panel, shows what a camera or a spot light sees (ADR-0116). An asset's right-click menu lists what uses it, renames or moves it with every file that names it, and edits a model's import settings (scale, walls and floors in the game) (ADR-0117). The editor draws with the system's font (`+editor.font`, `+editor.font_size`) and a dark theme (ADR-0118). F1 lists every shortcut: multiple selection with Ctrl+click, Ctrl+D to duplicate, copy and paste between levels, snapping (X), the grid (Shift+G), camera bookmarks (Ctrl+1 to 9 to set, 1 to 9 to go back). Drop glTF models or the faces of a sky (`name_front.png` and the five others) from the file manager onto the window to copy them into the open folder.

In development builds, editing a file in `engine/renderer/shaders/` reloads it while the sandbox runs; a shader that does not compile leaves the previous one running and shows the error in the overlay. The sandbox writes `sandbox.log` (the last run's stays as `sandbox.previous.log`) and, on Windows, crash dumps to `crashdumps/` in the working directory. At the next start each dump gets a readable report beside it, `<dump>.txt`, with the failing thread's stack, and the menu says where it is; `build/windows-msvc/apps/crash-report/Release/crash-report.exe <dump or folder>` prints reports (ADR-0080).

Settings are configuration variables (cvars). Every application reads the engine's settings from `daidalos.cfg` next to its executable, then a file of its own, such as the sandbox's `sandbox.cfg`. The build copies both, commented, from `config/daidalos.cfg` and `apps/sandbox/sandbox.cfg` the first time, and leaves them alone afterwards. The command line overrides them with `+name value`:

```bash
sandbox.exe +window.fullscreen true +renderer.antialiasing off
```

| Cvar | Default | Meaning |
|---|---|---|
| `window.width`, `window.height` | 1280, 720 | Main window size, in screen coordinates |
| `window.fullscreen` | false | Cover the display without borders, at its own resolution |
| `renderer.vsync` | true | Synchronize presentation with the display |
| `renderer.exposure_compensation` | 0 | Stops added to the scene's exposure: +1 is twice as bright |
| `renderer.anisotropy` | 16 | Anisotropic filtering of textures, from 1 to 16, read at startup |
| `renderer.normal_strength` | 1 | Relief of normal maps: 0 flattens them |
| `renderer.indirect_light` | true | Light that bounces off surfaces; off leaves direct light only |
| `renderer.reflections` | true | Ray-traced reflections; off, the indirect light stands in for them |
| `renderer.reflection_roughness` | 0.6 | Surfaces rougher than this trace no reflection |
| `renderer.volumetric_fog` | true | Fog lit by the lights, with light shafts; off, the air is clear |
| `renderer.traced_particles` | true | Particles in the ray-traced scene: smoke casts shadows, sparks show in mirrors; off (Low), rays pass them |
| `renderer.film_grain` | 0.35 | Film grain, from 0 (none) to 1 |
| `renderer.vignette` | 0.5 | Darker corners, as through a lens |
| `renderer.lens_dirt` | 0.5 | Smudges on the lens that bright light lights up |
| `renderer.chromatic_aberration` | 0 | Red and blue split towards the edges |
| `renderer.motion_blur` | 0 | Fraction of a frame the shutter is open; 0 for none |
| `renderer.log_gpu_times` | 0 | Logs each pass group's GPU time, averaged over 100 frames from this frame of the level; 0 for none |
| `renderer.fog_slices` | 64 | Slices of the volumetric fog in depth, from 16 to 256 |
| `renderer.restir_spatial` | true | Pixels reuse the lights their neighbours chose, as well as their own past |
| `renderer.restir_unbiased` | false | Test every reused light for shadows, to compare with the path tracer |
| `renderer.antialiasing` | `taa` | `taa` or `off` |
| `renderer.validation` | true in Debug | Enable the Vulkan validation layers, if installed |
| `view.fov` | 90 | Degrees across the screen, from 30 to 150 |
| `view.distance` | 150 | Meters: nothing farther is drawn or lit |
| `audio.volume`, `audio.effects`, `audio.music` | 1 | Volumes, from 0 to 1, read at startup |
| `input.mouse_sensitivity` | 0.0025 | Radians of turn per mouse unit |
| `input.rumble` | 1 | Strength of the gamepads' rumble, 0 (off) to 1 |

F12 saves a screenshot in `Screenshots/` beside `user.cfg`. In the sandbox, P (or the right stick's click) opens photo mode: a free camera, lens, exposure and film controls, the path tracer, and photos saved as PNG and EXR.

The sandbox adds its own:

| Cvar | Default | Meaning |
|---|---|---|
| `sandbox.frames` | 0 | Quit after this many frames; 0 runs until closed |
| `sandbox.scene` | `Factory/Factory.level.json` | Level file (ADR-0054), or glTF model, to load from `assets/` |
| `sandbox.darkness` | 1 | Multiplies the darkness; 0 shows the level |
| `sandbox.ambient` | 0.1 | Light that reaches everywhere in the dark; 0 is pitch black |
| `sandbox.flashlight` | false | Start with the flashlight on |
| `sandbox.aperture` | 0 | The camera's f-number for depth of field; 0 keeps everything sharp |
| `sandbox.focus` | 3 | Meters the camera is focused at |
| `sandbox.classic_music` | false | The old demo's music track instead of the score that follows the danger |
| `sandbox.continue` | false | Start from the last checkpoint; F6 saves, F9 loads |
| `game.language` | en | Language of the interface, subtitles and captions (a file of `assets/Text`) |
| `game.subtitles`, `game.captions` | true, false | Show what is said, and what sounds are heard |
| `renderer.color_vision` | normal | protanopia, deuteranopia or tritanopia correct the image |
| `view.camera_shake` | 1 | How much the camera shakes, 0 to 1 |
| `bind.key.<control>`, `bind.pad.<control>` | | A control's key or gamepad button by name (`E`, `MouseRight`, `PadWest`); empty for none. The options' Controls tab sets them |
| `sandbox.start` | | Where the player's eyes start: `x y z yaw pitch`, in meters and degrees |

## Releasing

A release is a pair of archives, Windows and Linux, built with the `*-shipping` presets: Release, without the developer tools or the tests ([ADR-0038](docs/adr/0038-releases.md)). To build one locally:

```bash
cmake --preset windows-msvc-shipping            # or linux-clang-shipping
cmake --build --preset windows-msvc-shipping
cpack --preset windows-msvc-shipping            # build/packages/daidalos-engine-next-<version>-windows-x64.zip
```

To release:

1. Set the new version in `project(VERSION)` in `CMakeLists.txt` and in `version-string` in `vcpkg.json`, and merge it into `main`.
2. Tag the merge and push the tag: `git tag v0.2.0 && git push origin v0.2.0`.
3. The Release workflow checks that the tag matches both files, builds the two archives, and creates a **draft** release with them, their checksums and notes generated from the merged pull requests.
4. Review the draft on GitHub, then publish it.

Starting the Release workflow by hand (Actions > Release > Run workflow) builds the archives as workflow artifacts without creating a release.

## Formatting

```bash
python scripts/format.py            # format all sources
python scripts/format.py --check    # what CI runs
```

## Repository layout

```text
engine/<module>/      engine modules (static libraries)
apps/                 executables: the sandbox, the editor, texture-tool, crash-report
samples/              the smallest programs built on the engine (level-viewer)
tests/unit/<module>/  unit tests, one executable per module
tests/gpu/<module>/   tests that need a Vulkan driver
tests/assets/         small assets of the tests, generated by a script and committed
config/               the engine's settings file, daidalos.cfg, copied next to each executable
cmake/                CMake helper functions
scripts/              development scripts
docs/                 conventions and decision records
assets/               the demo's level, and sample assets that are downloaded and not committed
```

## Licence

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option. See [ADR-0047](docs/adr/0047-dual-mit-apache-licence.md).

The demo's free textures, sounds and fonts keep their own licences: see [assets/README.md](assets/README.md) and [assets/Factory/LICENSE.md](assets/Factory/LICENSE.md). The zombie is a CC-BY 4.0 model, credited in [assets/Factory/Monsters/LICENSE.md](assets/Factory/Monsters/LICENSE.md).

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
