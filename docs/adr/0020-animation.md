# ADR-0020: Skeletal animation with our own small code

- **Status:** Accepted
- **Date:** 2026-11-02

## Context

Phase 6 needs zombies that wait, walk, strike and die, fans that turn, and a handgun that fires and reloads. The roadmap asks for skinned meshes from glTF, with blending between clips. The legacy models are MilkShape 3D files, whose joints move each vertex alone, and whose clips are frame ranges written in the game code. Nothing here needs root motion, inverse kinematics, ragdolls or animation graphs.

## Decision

- **Our own code, no library.** A new `animation` module samples glTF channels and blends two clips, in about 300 lines. It depends on `ecs`, `math` and `resources`, and `renderer` and `scene` depend on it.
- **Clips are glTF animations.** `resources` reads skins, `JOINTS_0`, `WEIGHTS_0` and animations: translation, rotation and scale, with step or linear keys. Cubic spline keys are read as linear ones, with a warning, and morph targets are ignored. Weights are normalized at load.
- **Joints are entities.** `renderer::Instantiate` already makes one entity per node, so joints are ordinary entities in the hierarchy. An `animation::Animator` on the model's root writes their `LocalTransform`, and the usual transform pass computes their world matrices. Anything can hang from a joint, such as a weapon in a hand, with no extra code.
- **One clip plays, the previous one fades out.** `Animator::Play` starts a clip with a loop flag, a speed, a start time and a fade. Nodes are blended per part: translation and scale linearly, rotation by spherical interpolation. Nodes that no clip moves keep what the game gave them. The scene runs every animator after its frame systems, with the frame's time step.
- **Skinning on the GPU, in the vertex shader.** A skinned mesh keeps its usual vertices and adds a storage buffer of four joints and weights per vertex, read by `SV_VertexID`. Extraction writes one matrix per joint into `SceneView::jointMatrices`, the joint's world matrix times its inverse bind matrix, and the instance data points at the first one. Rigid and skinned meshes share one pipeline and one vertex layout, and a rigid draw never reads a skin.
- **MilkShape models are baked to glTF.** `scripts/import_legacy_level.py` converts `zombie.ms3d`, `fan.ms3d` and `handgun.ms3d`, with the legacy frame ranges cut into named clips at their legacy speeds. It samples every frame with the legacy engine's own quaternion and matrix code, ported line by line, and checks that the glTF data moves each vertex where the legacy engine did. The level marks where each model stands with `model=<file>` in the node's name.

## Consequences

- Animation adds no dependency. glTF files made by Blender or other tools play the same way as the converted ones.
- A model with 20 joints costs 20 entities per instance. That is fine for tens of zombies, but hundreds of characters would want poses stored in one component.
- Every joint matrix is computed and uploaded every frame, for every skinned mesh, even far away or out of view. Culling (Phase 9) should skip both.
- Collisions and queries still use the bind pose of a skinned mesh. Hit boxes that follow joints come with weapons.
- Two clips at most: there are no layers, such as running legs under a firing torso, and no additive clips.
- Tangents are not skinned, because nothing reads them before normal mapping in Phase 7.

## Alternatives considered

- ozz-animation: fast and proven, with compression and SIMD sampling, but it brings its own offline format and build tools, for a handful of small clips.
- Skinning on the CPU, as the legacy engine did: simple, but it rewrites vertex buffers every frame.
- A compute pass that skins into a buffer once per frame: worth it when the same skinned mesh is drawn several times, by shadow passes for example. It can replace the vertex shader path in Phase 7 without changing the data.
- Poses in one component, with no joint entities: faster for crowds, but attaching anything to a joint would need its own code.
