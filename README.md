# Daidalos Engine Next

A modular C++20 real-time 3D engine for indoor environments, rendering with Vulkan 1.4 and hardware ray tracing on Windows and Linux.

The project is at an early stage. Phases 0 to 5 of the roadmap in [OVERVIEW.md](OVERVIEW.md) are done:

| Module | Contents |
|---|---|
| `core` | `Result` and `Error`, `DAI_ASSERT`, `DAI_TRY`, generational handles, frame timer, worker pool, configuration variables (cvars), profiling macros, logging over spdlog, and GLM with the engine's conventions: axes, TRS transforms, reverse-Z projections, bounding boxes |
| `platform` | SDL3 window, window and input events, Vulkan surfaces, process execution, crash dumps (Windows), and keyboard, mouse and gamepad state with actions |
| `resources` | Asset paths and caching, glTF models, PNG, JPEG and KTX2 images, as plain CPU data |
| `rhi` | Vulkan 1.4 device with ray queries, swapchain, buffers, textures with mip levels and BC compression, pipelines, command lists, batched uploads, deferred destruction |
| `renderer` | HDR rendering with PBR materials, normal maps, point and spot lights, clustered on the CPU, ray-traced shadows for every light, a sky, a reference path tracer, a depth pre-pass, ambient occlusion, particles simulated on the GPU and tonemapping, skinning, GPU timings, shader hot reload, and the Dear ImGui overlay in development builds |
| `physics` | Jolt behind a small API: static meshes, kinematic boxes, raycasts, character movement |
| `audio` | miniaudio behind a small API: clips, voices, 3D sounds, buses, silent without an output |
| `scene` | Entities with sparse-set components and a hierarchy, skeletal animation, the asset library, and the scene that ties them to the renderer, physics and sounds |
| `app` | Startup and shutdown in order, main loop, current scene |

The sandbox is a first-person demo in a dark factory, the remake of the legacy engine's demo: see [docs/DEMO.md](docs/DEMO.md).

## Documentation

- [OVERVIEW.md](OVERVIEW.md): vision, architecture, roadmap.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the engine is built today, with diagrams.
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

Dependencies (SDL3, spdlog, GoogleTest, tl-expected, Vulkan headers, volk, the Vulkan Memory Allocator, Slang, Dear ImGui, fastgltf, libktx, stb, and Tracy for the profile presets) are installed automatically by vcpkg in manifest mode during the CMake configure step. They are listed in `vcpkg.json`, and their versions are pinned by the registry baseline in `vcpkg-configuration.json`. The GPU tests also use NVIDIA's FLIP (BSD 3-Clause), whose single header is in `third_party/flip`.

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

In development builds, editing a file in `engine/renderer/shaders/` reloads it while the sandbox runs; a shader that does not compile leaves the previous one running and shows the error in the overlay. The sandbox writes `sandbox.log` and, on Windows, crash dumps to `crashdumps/` in the working directory.

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
| `renderer.antialiasing` | `taa` | `taa` or `off` |
| `renderer.validation` | true in Debug | Enable the Vulkan validation layers, if installed |
| `view.fov` | 90 | Degrees across the screen, from 30 to 150 |
| `view.distance` | 150 | Meters: nothing farther is drawn or lit |
| `audio.volume`, `audio.effects`, `audio.music` | 1 | Volumes, from 0 to 1, read at startup |
| `input.mouse_sensitivity` | 0.0025 | Radians of turn per mouse unit |

The sandbox adds its own:

| Cvar | Default | Meaning |
|---|---|---|
| `sandbox.frames` | 0 | Quit after this many frames; 0 runs until closed |
| `sandbox.scene` | `Factory/Factory.gltf` | Level to load from `assets/` |
| `sandbox.darkness` | 1 | Multiplies the darkness; 0 shows the level |
| `sandbox.ambient` | 0.1 | Light that reaches everywhere in the dark; 0 is pitch black |
| `sandbox.flashlight` | false | Start with the flashlight on |
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
apps/                 executables (sandbox)
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

[CC BY-NC-SA 4.0](LICENSE). See [ADR-0013](docs/adr/0013-licence.md).
