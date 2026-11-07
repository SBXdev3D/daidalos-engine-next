# ADR-0009: Shader language and compilation

- **Status:** Superseded by ADR-0031 (2026-11-07)
- **Date:** 2026-10-27

## Context

Shaders must be compiled to SPIR-V for Vulkan. Shader duplication between passes should be limited, and editing shaders must be fast during development.

## Decision

- Shaders are written in HLSL and compiled offline to SPIR-V with DXC, as a CMake build step. The SPIR-V is embedded in the binaries, so nothing depends on the working directory.
- Development builds support shader hot reload: a changed shader is recompiled and its pipelines are recreated without restarting. A shader that does not compile leaves the previous one running.
- Shared code goes in HLSL include files.

## Consequences

- HLSL keeps DirectX 12 possible later without rewriting shaders.
- DXC comes from vcpkg (amended 2026-10-30, see below).
- Shader compilation errors are reported with file and line.

## Alternatives considered

- GLSL: Vulkan-native, but ties shaders to Vulkan.
- Slang: modules and generics reduce duplication, but it is a less familiar language. To be reconsidered in Phase 6 if shader permutations become a problem.

## Amendment (2026-10-30): DXC from vcpkg, run as a program

**DXC comes from the `directx-dxc` vcpkg port**, not the Vulkan SDK. Its version is pinned by the vcpkg
baseline like every other dependency, and neither the build nor CI needs an SDK installed. The Vulkan SDK
remains useful on development machines for the validation layers.

**Hot reload runs the `dxc` executable** and the engine never links the compiler. The binaries stay small,
shipped builds have no trace of it, and a compiler crash cannot take the engine down. A reload blocks the
frame while it compiles, which is acceptable for a development tool.
