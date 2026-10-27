# ADR-0009: Shader language and compilation

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

Shaders must be compiled to SPIR-V for Vulkan. Shader duplication between passes should be limited, and editing shaders must be fast during development.

## Decision

- Shaders are written in HLSL and compiled offline to SPIR-V with DXC, as a CMake build step.
- Development builds support shader hot reload: a changed shader is recompiled and its pipelines are recreated without restarting.
- Shared code goes in HLSL include files.

## Consequences

- HLSL keeps DirectX 12 possible later without rewriting shaders.
- DXC comes from the Vulkan SDK.
- Shader compilation errors are reported with file and line.

## Alternatives considered

- GLSL: Vulkan-native, but ties shaders to Vulkan.
- Slang: modules and generics reduce duplication, but it is a less familiar language. To be reconsidered in Phase 6 if shader permutations become a problem.
