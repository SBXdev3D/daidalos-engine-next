# Daidalos Engine Next

A modular C++20 real-time 3D engine for indoor environments, rendering with Vulkan 1.3 on Windows and Linux.

The project is at an early stage: the build foundation, the `core` module (errors and assertions) and the `logging` module exist. See the roadmap in [OVERVIEW.md](OVERVIEW.md).

## Documentation

- [OVERVIEW.md](OVERVIEW.md): vision, architecture, roadmap.
- [docs/CONVENTIONS.md](docs/CONVENTIONS.md): naming, ownership, error handling, math and rendering conventions.
- [docs/adr/](docs/adr/README.md): architecture decision records.

## Requirements

| | Windows | Linux |
|---|---|---|
| Compiler | Visual Studio 2022 (MSVC 19.4x) with the "Desktop development with C++" workload | Clang 18 or later |
| Build tools | CMake 3.25+ | CMake 3.25+, Ninja |
| Dependencies | vcpkg | vcpkg |
| Formatting | clang-format 19 (bundled with Visual Studio's "C++ Clang tools" component) | clang-format 19 |

Dependencies (spdlog, GoogleTest, tl-expected) are installed automatically by vcpkg in manifest mode during the CMake configure step. They are listed in `vcpkg.json`, and their versions are pinned by the registry baseline in `vcpkg-configuration.json`.

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

```bash
cmake --preset linux-clang-debug               # or linux-clang-release
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

### Sanitizers

`windows-msvc-asan` (AddressSanitizer) and `linux-clang-asan` (AddressSanitizer and UndefinedBehaviorSanitizer) are available with the same three commands.

### Running the sandbox

```bash
build/windows-msvc/apps/sandbox/Debug/sandbox.exe   # Windows
build/linux-clang-debug/apps/sandbox/sandbox        # Linux
```

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
cmake/                CMake helper functions
scripts/              development scripts
docs/                 conventions and decision records
Core/                 legacy code from the previous engine, deleted as each part is replaced
```

## Licence

[CC BY-NC-SA 4.0](LICENSE). See [ADR-0013](docs/adr/0013-licence.md).
