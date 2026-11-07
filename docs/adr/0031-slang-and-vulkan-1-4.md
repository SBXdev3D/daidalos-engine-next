# ADR-0031: Slang shaders, Vulkan 1.4 and ray queries

- **Status:** Accepted
- **Date:** 2026-11-07
- **Supersedes:** ADR-0009 (shader language); amends ADR-0007 (Vulkan version, required features)

## Context

ADR-0028 moves rendering to ray tracing, on Vulkan 1.4 with Slang shaders. Step 9.1 changes the language, the API version and the device requirements, before any ray is traced, so that later steps only add techniques. It must not change a pixel.

## Decision

- **Slang replaces HLSL compiled by DXC.** `slangc` comes from the `shader-slang` vcpkg port, runs at build time and for hot reload, and the engine never links it, as ADR-0009 did for DXC. Its options are kept in one place in CMake (`DAI_SLANG_ARGUMENTS`) and mirrored by `kSlangArguments` in the hot reload: SPIR-V 1.6, entry points keep their names, column-major matrices, optimization level 2, warnings are errors.
- **The shaders keep their HLSL syntax.** Files are renamed `.slang` (compiled) and `.slangh` (included), and still share code with `#include`. Slang modules (`import`) wait for step 9.4, where one BSDF is shared by rasterization, ray queries and the path tracer.
- **Vertex and instance indices keep their Vulkan meaning**, through `SV_VulkanVertexID` and `SV_VulkanInstanceID`. Slang's `SV_VertexID` subtracts the base vertex, as Direct3D does; DXC did not, and skinning reads weights by the full index.
- **Descriptor aliasing stays**: the bindless storage buffer array is declared once per element type at the same binding. Slang warns about it (warning 39001), and that one warning is disabled.
- **The generated SPIR-V is embedded** by `cmake/EmbedSpirv.cmake`, which writes the byte array that DXC's `-Fh` used to write. `rhi::ToSpirv` is unchanged.
- **Vulkan 1.4** is required by the loader, the instance, the device and VMA.
- **Ray tracing is required through ray queries only**: `VK_KHR_acceleration_structure`, `VK_KHR_deferred_host_operations` and `VK_KHR_ray_query`, with buffer device addresses. The ray tracing pipeline and `VK_KHR_ray_tracing_position_fetch`, which ADR-0028 listed, are left out: the reference path tracer (9.4) runs as a compute shader with ray queries, so the RHI needs no shader binding tables, and vertex attributes come from the engine's own buffers anyway. A later step that needs either adds it then.
- **A device without them is rejected** with a message naming what it lacks. The sandbox shows startup failures in an error box (`platform::ShowErrorMessage`), since a player who starts it from a file browser has no console.

## Consequences

- Every layout was checked before the switch: the 29 entry points compiled by DXC and by Slang reflect identical bindings, offsets, matrix strides, locations and built-ins.
- Screenshots of the factory before and after are identical where nothing moves; the particles differ only as much as two runs of the same build do.
- GPU time, RTX 3090, 1280x720 Release, averaged over 29,700 frames, two runs each: meshes 0.060 ms with DXC, 0.051 ms with Slang; particle simulation 0.091 ms with both, once Slang compiles at `-O2` (0.105 ms at its default level); shadows 0.046 ms with DXC, 0.073 ms with Slang. The shadow pass is not investigated further: step 9.5 replaces it with ray queries.
- Linux CI installs Mesa from the kisak PPA so that lavapipe offers Vulkan 1.4 and ray queries, and prints what it supports.
- The DXC port is gone from `vcpkg.json`. The Slang port downloads Slang's released binaries.

## Alternatives considered

- Requiring the ray tracing pipeline now, as ADR-0028 wrote: a second way to trace rays, shader binding tables in the RHI, and one more extension a driver may lack, for a path tracer that does not need to be fast.
- Rewriting the shaders as Slang modules at once: more change in a step whose test is that nothing changes.
- Enabling `shaderDrawParameters` and keeping `SV_VertexID`: the meaning of the index would change silently.
