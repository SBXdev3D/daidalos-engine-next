# Daidalos Engine Next

A modular C++20 real-time 3D engine for indoor environments, rendering with Vulkan 1.3 on Windows and Linux.

The project is at an early stage. Phases 0 to 4 of the roadmap in [OVERVIEW.md](OVERVIEW.md) are done:

| Module | Contents |
|---|---|
| `core` | `Result` and `Error`, `DAI_ASSERT`, generational handles, frame timer, configuration variables (cvars), profiling macros |
| `logging` | Logging facade over spdlog |
| `math` | GLM with the engine's conventions: axes, TRS transforms, reverse-Z projections, bounding boxes |
| `platform` | SDL3 window, window and input events, Vulkan surfaces, process execution, crash dumps (Windows) |
| `input` | Keyboard, mouse and gamepad state (held, pressed and released this frame) |
| `ecs` | Entities with generational handles, sparse-set components, system schedule, hierarchy, transform propagation |
| `resources` | Asset paths and caching, glTF models, PNG, JPEG and KTX2 images, as plain CPU data |
| `rhi` | Vulkan 1.3 device, swapchain, buffers, textures with mip levels and BC compression, pipelines, command lists, deferred destruction |
| `renderer` | Mesh rendering with depth, materials, asset library, cameras, scene extraction from the ECS, shader hot reload |
| `debugui` | Dear ImGui overlay drawn through the RHI (development builds) |
| `app` | Ordered startup with rollback, main loop |

The sandbox draws a glTF model and a few cubes with a free camera and a debug overlay.

## Documentation

- [OVERVIEW.md](OVERVIEW.md): vision, architecture, roadmap.
- [docs/CONVENTIONS.md](docs/CONVENTIONS.md): naming, ownership, error handling, math and rendering conventions.
- [docs/adr/](docs/adr/README.md): architecture decision records.

## Requirements

| | Windows | Linux |
|---|---|---|
| Compiler | Visual Studio 2022 (MSVC 19.4x) with the "Desktop development with C++" workload | Clang 18 or later |
| Build tools | CMake 3.25+ | CMake 3.25+, Ninja, pkg-config |
| System packages | | X11 and Wayland development packages, for SDL3 (see below) |
| Dependencies | vcpkg | vcpkg |
| To run | A Vulkan 1.3 driver | A Vulkan 1.3 driver (`mesa-vulkan-drivers` provides lavapipe without a GPU) |
| Optional | [Vulkan SDK](https://vulkan.lunarg.com/), for the validation layers | `vulkan-validationlayers` |
| Formatting | clang-format 19 (bundled with Visual Studio's "C++ Clang tools" component) | clang-format 19 |

Dependencies (SDL3, spdlog, GoogleTest, tl-expected, Vulkan headers, volk, the Vulkan Memory Allocator, DXC, Dear ImGui, fastgltf, libktx, stb, and Tracy for the profile presets) are installed automatically by vcpkg in manifest mode during the CMake configure step. They are listed in `vcpkg.json`, and their versions are pinned by the registry baseline in `vcpkg-configuration.json`.

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

Run it from the repository root, where it finds `assets/`. The sample model is downloaded once with `python scripts/fetch_sample_assets.py`; without it, the sandbox only draws its cubes. See [assets/README.md](assets/README.md) for licences.

The sandbox draws a small scene and exits when the window is closed or Escape (or a gamepad's Back button) is pressed. W, A, S, D, Q and E move the camera, Shift goes faster, and the right mouse button looks around; a gamepad's sticks do the same. In development builds, editing a file in `engine/renderer/shaders/` reloads it while the sandbox runs; a shader that does not compile leaves the previous one running and shows the error in the overlay. It writes `sandbox.log` and, on Windows, crash dumps to `crashdumps/` in the working directory.

Settings are configuration variables (cvars). They are read from `sandbox.cfg` in the working directory, one `name value` per line, and can be overridden on the command line with `+name value`:

```bash
sandbox.exe +window.width 1920 +window.height 1080
```

| Cvar | Default | Meaning |
|---|---|---|
| `window.width` | 1280 | Main window width, in screen coordinates |
| `window.height` | 720 | Main window height, in screen coordinates |
| `renderer.vsync` | true | Synchronize presentation with the display |
| `renderer.validation` | true in Debug | Enable the Vulkan validation layers, if installed |
| `sandbox.frames` | 0 | Quit after this many frames; 0 runs until closed |
| `sandbox.scene` | `Lantern/glTF/Lantern.gltf` | Model to load from `assets/` |

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
cmake/                CMake helper functions
scripts/              development scripts
docs/                 conventions and decision records
assets/               sample assets, downloaded and not committed
patreon/              drafts of the development diary
```

## Licence

[CC BY-NC-SA 4.0](LICENSE). See [ADR-0013](docs/adr/0013-licence.md).
