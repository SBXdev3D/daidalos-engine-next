# ADR-0074: Linux validation without a Linux machine at hand

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 14 (ADR-0041) asks for the full sample and golden-image suite on Linux, platform differences fixed and contained in the platform layer, Linux packages, and no test disabled on Linux without a documented reason. The plan was to run it all under WSL2 on the development machine. Two things stood in the way: WSL is not installed there, and installing it needs an administrator's prompt and a restart, which an unattended session cannot give; and CI, whose Linux job built and tested every pull request, has not run since Phase 9.19 (GitHub billing), so nothing after it has been compiled by anything but MSVC.

## Decision

Do what can be checked from Windows now, and leave the Linux run to one script:

- **A Linux compile check from Windows** (`scripts/check_linux_compile.py`): an Ubuntu 24.04 header sysroot made from its packages (glibc, libstdc++ 14, X11, Wayland), then `clang++ -fsyntax-only --target=x86_64-linux-gnu` on each source with its target's include folders and defines. It found five errors MSVC accepted. Four are fixed: a struct (`LightRange`, now `LightSourceRange`) hidden by a function of the same name; a constexpr local used in a lambda without capture; two test fixtures whose nested option structs, with default member initializers, were default arguments inside their own class. The fifth, a missing `#include <algorithm>` in `tests/gpu/renderer/MeshPassTests.cpp` (for `std::max` of a list), waits: a stray compiler process holds that file open.
- **Case:** `scripts/check_asset_case.py`, a test (`assets.case_matches_files`) and a CI step check that every path the assets and code name matches its file's spelling, case included. It found none wrong; the same check over every `#include` found none either.
- **Platform differences in the platform layer:** the editor's own `#ifdef` for the sandbox's file name became `platform::ExecutableFileName`. What remains outside it is miniaudio's wide-character file API on Windows (in the audio module, two lines) and test helpers.
- **The same scenes on both systems:** `sandbox.smoke.*` tests play every scene the sandbox supports (the factory, the effects room, the showcase, the Cornell box, the mirror room, the dark corridor) for 150 frames with the validation layers on; an error or a crash fails them. They are labelled `smoke`, run one at a time, and CI's ASan job leaves them out (too slow on lavapipe).
- **Linux packages:** already a CPack TGZ (ADR-0038), with every library static; the vcpkg ports added since CI stopped (Steam Audio, Recast) support Linux.
- **The Linux run in one command:** `scripts/validate_linux.sh` installs the packages, builds, runs every test and the smoke tests (on a virtual display if there is none), and packages, writing a report.

Tests disabled on Linux, with why: the crash-dump tests (Linux leaves core dumps to the system, ADR-0015). No other.

## Consequences

- **Linux is unverified at run time.** The engine compiles for Linux as far as parsing goes, but has not been linked or run there since Phase 9.19. Running `scripts/validate_linux.sh` under WSL2 (`wsl --install -d Ubuntu`, as an administrator, then a restart) or on a Linux machine closes this; under WSL2 the GPU part depends on the driver's Vulkan support (dzn or the GPU vendor's), so lavapipe on a virtual display is the fallback.
- CI's step catches case mistakes as soon as billing lets it run again.
- The compile check reads the Windows build's projects, so a `#define` CMake sets only on Windows makes Windows-only code look broken; its two such reports (the crash helper and its tests) are known.

## Alternatives considered

- **Waiting for WSL before anything:** the phase would have stopped on a prompt no one was there to answer.
- **A full Linux cross-build from Windows:** every vcpkg dependency for x64-linux with a cross toolchain; long to set up for what WSL gives with one command.
