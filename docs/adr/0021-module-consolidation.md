# ADR-0021: Nine modules, each a layer or a wall around a library

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

After Phase 6 the engine had 15 modules for about 12,000 lines. Four were under 400 lines (`scene`, `logging`, `math`, `animation`), and some splits did not follow how the code was used: the renderer owned `AssetLibrary`, `Instantiate`, the ECS components and the extraction of the ECS into a `SceneView`, so it depended on `ecs` and `animation`. Physics and audio depended on `ecs` for one sync function each. Reading the code meant jumping between many small libraries, and the dependency graph was harder to explain than the engine itself.

## Decision

- **A module exists only if it is a real layer, or if it hides a large third-party library.** That gives nine:

  | Module | Holds | Reason |
  |---|---|---|
  | `core` | core, logging and math | The foundation; hides spdlog, and carries GLM and tl::expected |
  | `platform` | platform and input | Hides SDL; input only turns platform events into state |
  | `resources` | unchanged | Hides fastgltf, KTX and stb |
  | `rhi` | unchanged | Hides Vulkan, volk and VMA |
  | `renderer` | renderer and the debug overlay | Draws a `SceneView`; hides Dear ImGui |
  | `physics` | unchanged | Hides Jolt |
  | `audio` | unchanged | Hides miniaudio |
  | `scene` | ecs, animation, scene, the asset library, the components and the systems that tie the ECS to the rest | The game world |
  | `app` | unchanged | The main loop |

- **The renderer does not know about the ECS.** It draws a `SceneView` of copies. Turning entities into that view, loading models into entities, and the `Camera`, `MeshRenderer`, `SkinnedMeshRenderer`, `NodeName`, `KinematicBody` and `SoundEmitter` components belong to `scene`, in the `scene` namespace.
- **Physics and audio are leaves.** They depend on `core` only. `scene::SyncKinematicBodies` and `scene::SyncSoundEmitters` connect them to entities.
- **Include paths follow namespaces, not modules.** A module can own several namespaces: `<daidalos/math/Transform.hpp>` is in `core`, `<daidalos/ecs/World.hpp>` and `<daidalos/animation/Animation.hpp>` are in `scene`. Code reads `math::Transform` and `ecs::World` as before.
- **Test executables follow the modules**: eight unit test executables and three GPU ones, instead of sixteen.

## Consequences

- The dependency graph has nine boxes and fits in one sentence: `core` at the bottom, `platform`, `resources` and `rhi` above it, `renderer`, `physics` and `audio` above those, then `scene`, then `app`.
- A change to the ECS rebuilds `scene` and what uses it, not the renderer.
- `core` now carries GLM's configuration macros, so every module sees the same depth range and angle units without linking a separate `math` target.
- A module is larger, so its folder holds more files. Its namespaces keep them apart.

## Alternatives considered

- Keeping fifteen modules: each one was small and clear on its own, but the whole was harder to read than the code it organized.
- Folding physics and audio into `scene` too: seven modules, but Jolt needs `-fno-sanitize=vptr` and miniaudio needs its warnings off, which per-file flags inside a larger target would make easy to break.
- Namespaces that follow modules (`core::Transform`, `scene::World`): stricter, but a much larger change for names that read worse.
