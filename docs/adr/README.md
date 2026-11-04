# Architecture Decision Records

Each record explains one decision with lasting consequences: the context, the decision, its consequences, and the alternatives. A record is not edited to reverse a decision; a new record supersedes it.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-build-system-and-language.md) | CMake + presets, vcpkg, C++20 | Accepted |
| [0002](0002-testing.md) | GoogleTest + CTest, real Vulkan drivers for graphics tests | Accepted |
| [0003](0003-error-handling.md) | `Result<T>` with `tl::expected`, no exceptions across modules | Accepted, amended |
| [0004](0004-platform-layer.md) | SDL3 behind concrete wrappers | Accepted |
| [0005](0005-math-and-coordinates.md) | GLM, right-handed, Y-up, reverse-Z | Accepted |
| [0006](0006-ecs.md) | Custom sparse-set ECS with generational handles | Accepted |
| [0007](0007-rendering-api.md) | Vulkan 1.3 as the single rendering API, concrete RHI | Accepted, amended |
| [0008](0008-clustered-forward.md) | Clustered forward rendering | Accepted, amended |
| [0009](0009-shaders.md) | HLSL compiled to SPIR-V with DXC from vcpkg, hot reload | Accepted, amended |
| [0010](0010-asset-formats.md) | glTF 2.0 + KTX2 in UASTC, transcoded at load | Accepted, amended |
| [0011](0011-threading-model.md) | Main-thread logic, one asset worker pool | Accepted |
| [0012](0012-indoor-visibility.md) | Frustum + BVH, portals only if measured, no BSP | Accepted |
| [0013](0013-licence.md) | CC BY-NC-SA 4.0 | Accepted |
| [0014](0014-repository-and-ci.md) | GitHub, GitHub Actions, delete legacy code | Accepted |
| [0015](0015-development-tooling.md) | Tracy, minidumps, cvars, ImGui, hot reload early | Accepted |
| [0016](0016-resources.md) | Typed loaders, caches by identity, libraries that own their assets | Accepted, amended |
| [0017](0017-scenes.md) | Scenes own a world and its assets, glTF plus code, no scene file format | Accepted |
| [0018](0018-physics.md) | Jolt with virtual characters, one physics world per scene | Accepted, amended |
| [0019](0019-audio.md) | miniaudio, an engine per application and a sound world per scene | Accepted |
| [0020](0020-animation.md) | Own skeletal animation, joints as entities, skinning in the vertex shader | Accepted, amended |
| [0021](0021-module-consolidation.md) | Nine modules, each a layer or a wall around a library | Accepted |
| [0022](0022-lighting-and-materials.md) | HDR, PBR, legacy light units, glTF normal maps with MikkTSpace | Accepted, amended |
| [0023](0023-shadows.md) | Spot-light shadows: four per frame in one atlas, nearest first | Accepted |
| [0024](0024-prepass-and-ambient-occlusion.md) | Depth pre-pass, half-resolution SSAO, ambient light from above and below | Accepted |
| [0025](0025-gpu-particles.md) | Particles emitted, simulated, sorted and drawn on the GPU | Accepted |
| [0026](0026-engine-settings-and-antialiasing.md) | The engine's settings file, daidalos.cfg; shadows, view and antialiasing settings; FXAA and MSAA | Accepted |
