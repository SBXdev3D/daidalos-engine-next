# ADR-0001: Build system, dependencies and language standard

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

The repository used a Visual Studio solution with NuGet packages committed to git. The engine must build on Windows and Linux, and builds must be reproducible.

## Decision

- CMake with `CMakePresets.json` (Windows MSVC and Linux Clang presets, Debug and Release, plus an AddressSanitizer preset).
- vcpkg in manifest mode (`vcpkg.json` with a pinned baseline) for all third-party libraries.
- C++20, set explicitly in CMake. C++ modules are not used.
- Warnings are errors (`/W4 /permissive-` on MSVC, `-Wall -Wextra` on Clang/GCC).

## Consequences

- The `.sln` and `.vcxproj` files, `packages/` and build output folders are deleted. Visual Studio opens the CMake project directly.
- The same presets are used locally and in CI.
- Adding a dependency means editing `vcpkg.json`; nothing third-party is committed.

## Alternatives considered

- Keep the Visual Studio projects: Windows-only.
- CMake FetchContent for everything: slower configures and no binary caching. It remains acceptable for a library that vcpkg does not provide.
- Conan: more infrastructure than needed.
