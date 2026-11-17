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
| [0007](0007-rendering-api.md) | Vulkan as the single rendering API (1.4 from Phase 9), concrete RHI | Accepted, amended |
| [0008](0008-clustered-forward.md) | Clustered forward rendering | Superseded by 0044 |
| [0009](0009-shaders.md) | HLSL compiled to SPIR-V with DXC from vcpkg, hot reload | Superseded by 0031 |
| [0010](0010-asset-formats.md) | glTF 2.0 + KTX2 in UASTC, transcoded at load | Accepted, amended |
| [0011](0011-threading-model.md) | Main-thread logic, one asset worker pool | Accepted |
| [0012](0012-indoor-visibility.md) | Frustum + BVH, portals only if measured, no BSP | Accepted |
| [0013](0013-licence.md) | CC BY-NC-SA 4.0 | Superseded by 0047 |
| [0014](0014-repository-and-ci.md) | GitHub, GitHub Actions, delete legacy code | Accepted |
| [0015](0015-development-tooling.md) | Tracy, minidumps, cvars, ImGui, hot reload early | Accepted |
| [0016](0016-resources.md) | Typed loaders, caches by identity, libraries that own their assets | Accepted, amended |
| [0017](0017-scenes.md) | Scenes own a world and its assets, glTF plus code, no scene file format | Accepted |
| [0018](0018-physics.md) | Jolt with virtual characters, one physics world per scene | Accepted, amended |
| [0019](0019-audio.md) | miniaudio, an engine per application and a sound world per scene | Accepted |
| [0020](0020-animation.md) | Own skeletal animation, joints as entities, skinning in the vertex shader | Accepted, amended |
| [0021](0021-module-consolidation.md) | Nine modules, each a layer or a wall around a library | Accepted |
| [0022](0022-lighting-and-materials.md) | HDR, PBR, legacy light units, glTF normal maps with MikkTSpace | Accepted, amended; light units superseded by 0043, forward mesh pass by 0044 |
| [0023](0023-shadows.md) | Spot-light shadows: four per frame in one atlas, nearest first | Superseded by 0035 |
| [0024](0024-prepass-and-ambient-occlusion.md) | Depth pre-pass, half-resolution SSAO, ambient light from above and below | Partly superseded by 0040; the pre-pass is the G-buffer of 0044 |
| [0025](0025-gpu-particles.md) | Particles emitted, simulated, sorted and drawn on the GPU | Accepted, amended by 0053 |
| [0026](0026-engine-settings-and-antialiasing.md) | The engine's settings file, daidalos.cfg; shadows, view and antialiasing settings; FXAA and MSAA | Accepted, antialiasing superseded by 0037 |
| [0027](0027-game-interface.md) | The game's interface: an immediate-mode canvas, distance field fonts, drawn after antialiasing | Accepted |
| [0028](0028-ray-traced-rendering.md) | Ray-traced rendering: mandatory hardware ray tracing, Vulkan 1.4, Slang, own denoiser, no raster fallback | Accepted, amended |
| [0029](0029-menus-pause-and-player-settings.md) | Menus: immediate-mode widgets, scenes that pause, the player's settings in user.cfg | Accepted |
| [0030](0030-developer-tools.md) | Developer tools: console, statistics, entity inspector and resources, on the ImGui overlay | Accepted |
| [0031](0031-slang-and-vulkan-1-4.md) | Slang shaders, Vulkan 1.4, hardware ray tracing through ray queries | Accepted |
| [0032](0032-render-graph.md) | A render graph that places barriers, labels and times passes | Accepted |
| [0033](0033-acceleration-structures.md) | Acceleration structures, refitted skinned draws, skinning once per frame | Accepted |
| [0034](0034-reference-path-tracer.md) | A reference path tracer, one BSDF in a Slang module, storage images | Accepted |
| [0035](0035-ray-traced-shadows.md) | Ray-traced shadows for every light, FLIP in the tests | Accepted, amended |
| [0036](0036-sky.md) | A cube-texture sky, where nothing is drawn and for rays that leave the scene | Accepted |
| [0037](0037-temporal-antialiasing.md) | Motion vectors, jitter and temporal antialiasing, replacing FXAA and MSAA | Accepted, amended by 0049 |
| [0038](0038-releases.md) | Releases: a tag builds Windows and Linux archives into a draft GitHub release | Accepted |
| [0039](0039-denoiser-and-soft-shadows.md) | An own spatiotemporal denoiser of the lights' diffuse and specular light; lights with a radius and soft shadows | Accepted, amended by 0044 |
| [0040](0040-diffuse-global-illumination.md) | Diffuse global illumination from an own radiance cache, replacing ambient light and SSAO; directional lights and the moon | Accepted, amended by 0045 |
| [0041](0041-engine-technical-spec.md) | The engine technical spec: what joins the roadmap (physical units, presets, a level editor, materials and atmosphere, sound), what changes, what stays out | Accepted |
| [0042](0042-reflections-and-glass.md) | Ray-traced reflections along each pixel's specular lobe, followed by the denoiser; glass drawn over the frame | Accepted |
| [0043](0043-physical-light-units.md) | Physical light units: candela, lux, kelvin, nits and EV100, pre-exposed on the GPU; level lights in KHR_lights_punctual | Accepted |
| [0044](0044-restir-di-and-deferred-lighting.md) | ReSTIR DI over lights and emissive triangles sampled by power; deferred lighting from a thin G-buffer, replacing light clusters and the forward mesh pass | Accepted |
| [0045](0045-restir-gi.md) | One bounce lit by the lights before the radiance cache, clamped bounces, and the dark corridor test; ReSTIR GI measured and dropped | Accepted |
| [0046](0046-texture-tool-and-factory-textures.md) | A KTX2 encoder and `texture-tool`; the factory's 2048 px albedo, normal, roughness and metalness maps in UASTC, height maps kept for Phase 11 | Accepted |
| [0047](0047-dual-mit-apache-licence.md) | Dual MIT or Apache 2.0 licence for the code and the owner's assets; the NVIDIA SDKs no longer excluded by the licence, own implementations kept | Accepted |
| [0048](0048-image-formation.md) | Image formation: auto-exposure within a range each level sets, bloom, AgX, HDR10 output, the interface on a layer, brightness calibration | Accepted, amended by 0049 |
| [0049](0049-upscaling-and-dynamic-resolution.md) | The engine's own temporal upscaling and sharpening, targets drawn into a corner for dynamic resolution, and the test that decides on FSR 3.1, which adds it as step 9.16 | Accepted, amended by 0050 |
| [0050](0050-fsr-3-1.md) | AMD FSR 3.1's passes, unchanged, run by the engine through its own callbacks; a little behind the engine's upscaler by FLIP, steadier, kept as an option | Accepted |
| [0051](0051-volumetric-fog.md) | Volumetric fog in froxels lit by the lights with one shadow ray, boxes of fog that may glow, 3D textures in the RHI, fog in the path tracer; ReSTIR reuse per froxel measured and dropped | Accepted |
| [0052](0052-quality-presets-and-pipelines.md) | Quality presets of the settings the engine has, chosen at the first launch by measuring the GPU; a pipeline cache on disk; no pipeline created after loading; video memory from VK_EXT_memory_budget | Accepted |
| [0053](0053-particles-2.md) | Particles 2.0 before the editor, steps 9.19 to 9.25: effects as data files, flipbooks and curves, forces, collisions against the acceleration structure, GPU sub-emitters and hits read back, emitter lights, distortion, ribbons, mesh particles, weather zones; the engine's own flipbook tool | Accepted |
| [0054](0054-level-files.md) | Level files: `<name>.level.json`, an environment and entities with ids, parents, transforms, typed components and game tags; the factory converted, its props in their own glTF files; the moon, alarm beams, dust and steam as data | Accepted |
| [0055](0055-level-editor.md) | The level editor: its own executable in development builds, ImGui docking and ImGuizmo, the scene drawn into the dockspace's central node (`SceneView::viewport`), picking on the CPU against the physics triangles, the level as the document | Accepted |
| [0056](0056-layered-materials.md) | Layered materials: a clear coat and a sheen over the base in the one BSDF, a 64-bit G-buffer target for them, `KHR_materials_clearcoat` and `KHR_materials_sheen` | Accepted |
| [0057](0057-lights-profiles-animation.md) | Light profiles (IES) and cookies in the one attenuation function, animated lights and power circuits driving the light and its lamp's emission together, area lights as emissive meshes, presets | Accepted |
| [0058](0058-flashlight.md) | The flashlight: a profile and a lens for its beam, held in the left hand, a spring that trails the camera's turns and a walking sway, a battery, a failure hook | Accepted |
| [0059](0059-ray-visibility.md) | Which rays see what: a mask bit per kind of ray, draw flags for the camera, reflections and bounced light, `RayVisibility` on an entity for what is under it | Accepted |
| [0060](0060-decals.md) | Decals in the material function: boxes that project images, applied in the G-buffer and at ray hits, 64 per frame with a mask per draw, level decals and impacts that fade | Accepted |
| [0061](0061-wetness.md) | Wetness in the material function: wet volumes, porosity, a water coat, puddles in low spots rippled by rain and the game, streaks on walls, drying | Accepted |
| [0062](0062-particles-in-rays.md) | Particles in rays: a box per pool slot in a rebuilt BLAS, discs tested in the ray loops with stochastic opacity, shadows and reflections, opt-out per effect; smoke splatted into the fog's froxels | Accepted |
