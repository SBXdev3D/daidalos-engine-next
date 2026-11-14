# Architecture

How the engine is built today, through Phase 9.13: real lights and emissive surfaces with soft ray-traced shadows, sampled by ReSTIR DI over a G-buffer, indirect light from a bounce lit by the lights and a radiance cache, ray-traced reflections and glass, their denoiser, temporal antialiasing, a sky, PBR materials, normal maps and particles on the GPU. [OVERVIEW.md](../OVERVIEW.md) holds the vision and the roadmap, and [the ADRs](adr/README.md) hold the reasons for each decision. This page describes what the code does now, and it changes in the same pull request as the code.

## Modules

Each module is a static library in `engine/<module>/`. A module exists only if it is a real layer, or if it hides a large third-party library (ADR-0021). An arrow means "links to". The graph comes from the `CMakeLists.txt` files, and it has no cycles.

```mermaid
flowchart TD
    sandbox["apps/sandbox"] --> app
    app --> scene
    scene --> renderer
    scene --> physics
    scene --> audio
    renderer --> rhi
    renderer --> platform
    renderer --> resources
    rhi --> core
    platform --> core
    resources --> core
    physics --> core
    audio --> core
```

Every module also links to `core`; the graph only shows it for the bottom row.

| Module | Responsibility | Third-party libraries it hides |
|---|---|---|
| `core` | `Result`, `Error`, `DAI_ASSERT`, `DAI_TRY`, handles, slot pool, frame timer, worker pool, cvars, profiling macros, logging (`logging::`), conventions, transforms, projections and boxes (`math::`) | spdlog; GLM, tl::expected and Tracy are public on purpose |
| `platform` | Window, events, Vulkan surface, processes, crash dumps, and keyboard, mouse and gamepad state with actions (`input::`) | SDL3 |
| `resources` | Asset paths, caches, decoding of files into plain CPU data | fastgltf, KTX, stb |
| `rhi` | Vulkan 1.4 device with ray queries, swapchain, buffers, textures, pipelines, command lists, batched uploads | Vulkan, volk, VMA |
| `renderer` | Draws a `SceneView`: G-buffer, lighting, skinning, materials, shader hot reload, the game's interface (`ui::`), and the debug overlay (`debugui::`, development builds) | Dear ImGui, stb_truetype (in `resources`) |
| `physics` | Collisions, characters and ray queries of a scene | Jolt |
| `audio` | Output, mixing, clips and voices of a scene, silent without an output | miniaudio, stb_vorbis |
| `scene` | Entities (`ecs::`), animation (`animation::`), the asset library, the engine's components, the systems that tie entities to the renderer, physics and sounds, and the scene itself | |
| `app` | Startup and shutdown in order, main loop, current scene, the player's settings, the developer console and tools (development builds) | |

Headers are included by namespace, not by module: `<daidalos/math/Transform.hpp>` lives in `core`, and `<daidalos/ecs/World.hpp>` in `scene`.

Three rules keep this graph healthy:

- **Only `scene` knows about entities.** The renderer draws a `SceneView` of copies, and physics and audio know bodies and voices. `scene::ExtractScene`, `SyncKinematicBodies` and `SyncSoundEmitters` connect them to the ECS.
- **Nothing above `rhi` knows about Vulkan.** `rhi` hides it completely, with concrete classes and no virtual interface (ADR-0007). It does not depend on `platform`: it receives a callback that creates the surface, so it can run with no window, which the GPU tests rely on.
- **`resources` does not depend on `rhi`.** It produces plain CPU data, and `scene::AssetLibrary` turns that into GPU objects through the renderer (ADR-0016).

## Startup and shutdown

`app::Application::Start` starts each step in order. If a step fails, `Stop` releases whatever is started, in reverse order, so nothing runs half-initialized. A normal shutdown calls the same `Stop`.

```mermaid
flowchart LR
    A[logging] --> B[crash handler] --> C[configuration] --> D[platform] --> E[main window] --> W[worker pool]
    W --> AU[audio] --> F[renderer] --> G[debug overlay] --> H["application<br/>OnStart"]
    H -. "failure or shutdown:<br/>scene first, then reverse order" .-> A
```

The configuration step reads the engine's settings from `daidalos.cfg` next to the executable, then the files a game adds, such as `sandbox.cfg`, then the player's own `user.cfg`, then the command line; later ones win (ADR-0026). The build copies `config/daidalos.cfg` next to every application the first time. Settings that the renderer and the audio can follow while running, such as antialiasing, the field of view and the volumes, are applied every frame; the others are read at startup. A game's options change settings with `Application::ChangeSetting`, and `Application::SaveSettings` writes them to `user.cfg`: the ones it held and the ones changed, never values that only came from the command line (ADR-0029).

The game plugs in through `app::IApplicationHandler`: `OnStart`, `OnEvent`, `OnFixedUpdate`, `OnUpdate`, `OnRender` and `OnStop`. It usually gives the application a `scene::Scene`, which the application then runs. `Stop` releases the scene before anything else, because its assets and sounds belong to the renderer and the audio engine, even if `OnStart` failed halfway. A machine with no audio output only gets a warning, and its scenes stay silent (ADR-0019).

## One frame

Everything here runs on the main thread, except the jobs of the worker pool (ADR-0011). The GPU runs in parallel, with up to two frames in flight.

```mermaid
sequenceDiagram
    participant A as Application
    participant G as Game handler
    participant S as Current scene
    participant R as Renderer
    participant D as rhi::Device

    A->>A: Swap in the pending scene
    A->>A: Read events, update InputState
    A->>G: OnEvent, for each event
    A->>A: Deliver finished worker jobs
    A->>A: Advance the frame timer
    loop 0 to n fixed steps
        A->>G: OnFixedUpdate
        A->>S: Fixed systems, then physics
    end
    A->>G: OnUpdate
    A->>S: Frame systems, then animators
    A->>S: Extract: transforms, SceneView with current and previous matrices, particle spawns, sounds and listener
    A->>G: OnRender
    A->>R: Render(SceneView, overlay)
    R->>R: Reload changed shaders
    R->>D: BeginFrame: send the pending uploads, read last timestamps
    R->>D: Skinning, with the previous joints too, then the frame's acceleration structures
    R->>D: Emissive lights: new emissive meshes averaged once, the frame's emissive triangles placed (compute)
    R->>D: G-buffer, jittered: depth, normals, motion vectors, base color, emission
    R->>D: Radiance cache: update, resolve; indirect light: one ray per pixel, its hit lit by the lights (compute)
    R->>D: Reflections: one ray per pixel along its specular lobe (compute)
    R->>D: Particles: emit, simulate, sort (compute)
    R->>D: Direct lighting: ReSTIR DI, then one shadow ray per pixel, plus indirect light and reflections (compute)
    R->>D: Denoiser: the light averaged over frames, then over neighbours (compute)
    R->>D: Composite: the light put back together, the sky and the fog, into the HDR target
    R->>D: Particles drawn back to front, one indirect draw, and their coverage as a reactive mask
    R->>D: Glass, if any: the frame copied, then glass drawn over it, reading the copy and tracing rays
    R->>D: Temporal resolve: the frame blended with the reprojected history (compute)
    R->>D: Tonemap into the back buffer, then interface and overlay
    R->>D: EndFrame: submit and present
```

`SceneView` holds copies, so rendering never touches the ECS. World transforms are recomputed from scratch every frame, during extraction, with no dirty flags (ADR-0006).

## From file to GPU

```mermaid
flowchart LR
    F["glTF, PNG, JPEG, KTX2<br/>under assets/"] -->|"LoadModelFile<br/>LoadImageFile<br/>on a worker"| C["ModelData, ImageData<br/>plain CPU data"]
    C -->|"AssetLibrary<br/>on the main thread"| G["Meshes, materials, textures<br/>owned by handles,<br/>skins and clips"]
    G -->|"scene::Instantiate"| E["Entities with LocalTransform,<br/>MeshRenderer or SkinnedMeshRenderer,<br/>Animator on the root"]
    E -->|"scene::ExtractScene"| S["SceneView<br/>DrawItem list"]
    S -->|"MeshPass::Record"| V["GPU commands"]
```

An asset's identity is its normalized path relative to the asset root. The case must match the file on disk, so a mismatch fails on Windows as well as on Linux. A shared texture is uploaded once. `LoadModel` does all of this at once, and `LoadModelAsync` keeps the main thread running during the first arrow. The uploads of the second arrow are only recorded: they reach the GPU together, ahead of the next frame, and nothing waits for them. Loading a replacement level happens in a second scene, which takes over when it is ready.

The sandbox's level comes from the legacy engine, through `scripts/import_legacy_level.py`: a single glTF file, whose node names tell the game what each node is (ADR-0017). Animated models have their own glTF files, which the level points to with `model=` in a node's name.

## Animation

Joints are entities, so animation only writes transforms, and everything else is the usual path (ADR-0020).

```mermaid
flowchart LR
    A["Animator<br/>on the model's root"] -->|"samples and blends clips"| L["LocalTransform<br/>of joint entities"]
    L -->|"PropagateTransforms"| W["WorldTransform"]
    W -->|"ExtractScene"| J["jointMatrices:<br/>world × inverse bind"]
    J --> V["Skinning pass, compute:<br/>four joints per vertex,<br/>by weight, once per frame"]
    V --> D["Skinned vertices<br/>drawn by every pass,<br/>traced by rays"]
```

## Who owns what

```mermaid
flowchart TD
    App["Application"] --> Renderer
    Renderer --> Device["rhi::Device<br/>GPU memory, deferred destruction"]
    Renderer --> MeshPass["MeshPass<br/>meshes, materials, pipelines"]
    App --> Scene["scene::Scene"]
    Scene --> Library["AssetLibrary<br/>releases all its assets together"]
    Scene --> World["ecs::World<br/>entities and components"]
    Scene --> Physics["physics::PhysicsWorld<br/>bodies and characters"]
    Scene --> Sounds["audio::SoundWorld<br/>clips and voices"]
    App --> Audio["audio::AudioEngine<br/>output and mixer"]
    Sounds -. "plays through" .-> Audio
    Library -. "handles" .-> MeshPass
    Library -. "handles" .-> Device
    World -. "handles" .-> MeshPass
```

Solid arrows own, and dotted arrows only refer. GPU objects are never held by pointer outside their owner. Everyone else holds a generational handle, which detects when its slot was freed or reused:

- A draw with a destroyed mesh is skipped, and a draw with a destroyed material uses the default one, so unloading assets before entities is safe.
- `Destroy*` invalidates the handle at once. The GPU object is released when the timeline semaphore shows that the GPU has finished with it.
- The renderer must outlive every `AssetLibrary`, which `Application::Stop` guarantees by releasing the scene first. There is no reference counting and no eviction (ADR-0016).

## How the GPU reads its data

The renderer uses bindless access: one descriptor set holds every texture, cube texture, storage buffer, storage image and top-level acceleration structure, and each draw only pushes indices. A ray's hit finds its draw's InstanceData by its custom index.

```mermaid
flowchart LR
    P["Push constants<br/>buffer and element indices"] --> FB["FrameData<br/>view-projection, fog, sky,<br/>indirect light, reflections"]
    FB --> LB["LightData<br/>the analytic lights"]
    FB --> AT["Alias tables<br/>lights and emissive<br/>draws by power"]
    FB --> ET["Emissive triangles<br/>in world space"]
    P --> IB["InstanceData<br/>world matrix, vertices,<br/>indices, material"]
    IB --> VB["Vertices and indices<br/>of the mesh, or its<br/>skinned vertices"]
    P --> MB["MaterialData<br/>factors and texture indices"]
    MB --> T["Bindless texture array"]
    FB --> AS["The frame's acceleration<br/>structure, by index"]
    FB --> CT["Bindless cube array<br/>the sky"]
```

## Lighting

Meshes are shaded in linear HDR with metallic-roughness PBR, the frame's lights and the light that bounces between surfaces, then tonemapped (ADR-0022, ADR-0040). Lights are `scene::Light` entities: point, spot or directional. Spot and directional lights shine down their entity's local -Z, so a lamp is placed and turned like anything else, and a light parented to the camera follows it. A directional light, such as the factory's moon, is infinitely far and has an angular radius instead of a radius. Levels bring their lights in glTF's `KHR_lights_punctual`, which become `scene::Light`s on their nodes' entities.

```mermaid
flowchart LR
    E["scene::Light<br/>on entities"] -->|"ExtractScene"| L["SceneView::lights<br/>and emissive draws"]
    L -->|"by power, on the CPU"| AT["Alias tables"]
    P["G-buffer<br/>depth, normals, roughness,<br/>motion, base color, emission"] --> M
    AT --> M["Direct lighting<br/>ReSTIR DI: 8 candidates,<br/>temporal and spatial reuse"]
    AS["Acceleration structure<br/>of the frame"] -->|"one shadow ray<br/>per pixel"| M
    AS -->|"update paths, one ray<br/>per pixel, one along<br/>its specular lobe"| GI["Global illumination<br/>a bounce lit by the lights,<br/>then the radiance cache"]
    AT --> GI
    P --> GI
    GI -->|"indirect light and<br/>reflection per pixel"| M
    M -->|"diffuse and specular<br/>light, apart"| DN["Denoiser<br/>temporal, then spatial"]
    P --> DN
    P -->|"emission,<br/>base color"| CO["Composite<br/>sky and fog"]
    DN --> CO
    CO --> H["HDR target<br/>Rgba16Float"]
    H -->|"copied, read behind<br/>glass, rays traced"| GL["Glass pass"]
    GL --> H
    H -->|"temporal resolve,<br/>PBR Neutral"| S["Swapchain<br/>sRGB"]
```

Light is in physical units (ADR-0043): point and spot lights in candela, directional lights in lux, with a tint and an optional color temperature in kelvin; emission and the sky in nits. A light falls off with the inverse square of the distance, faded to zero at its range by a smooth window, and shading is the illuminance times the BSDF. The scene sets its exposure in EV100 (the factory's is 3, a dim interior), and the player adds stops to it. Lights, emission and the sky reach the GPU already multiplied by the exposure, so the targets, the cache and the denoiser hold values near the screen's. Normal maps follow glTF (green up, bitangent from the tangent's `w`), and meshes that lack tangents get MikkTSpace ones at load. Lighting is deferred (ADR-0044): one raster pass, the G-buffer, writes each pixel's depth, normal and roughness, motion, base color and metalness, emission and occlusion, and compute passes light it. Lamps and the triangles of emissive meshes are lights alike: an emissive material lights the scene unless its glTF extras say `{"light": false}`. Each frame, the CPU builds an alias table that draws lights by power, and each emissive draw has one of its own over its triangles, whose emission a compute pass averages from the texture once. ReSTIR DI (Bitterli et al. 2020) then gives each pixel one light: eight candidates drawn by power and weighed by what they would send, one kept and tested for shadows, combined with what the same surface kept last frame and with three neighbours on the same surface, and one shadow ray towards it. Whatever the number of lights, a pixel casts one shadow ray; lights keep their names from frame to frame, so a pixel's choice follows its light. `renderer.debug_view lights` shows each pixel's light. No light comes from nowhere: what no light reaches, directly or after bouncing, is black. Indirect light comes from a radiance cache, a hash grid of world space whose cells hold the light surfaces send (ADR-0040). Each frame, one path from the camera per 4×4 pixels adds to the cells of the surfaces it meets the light of one light, picked among eight, and what it brings back from farther. Its last ray reads the cache, so light bounces on frame after frame. Then each pixel casts one ray, in a random direction around its normal (ADR-0045). Where it hits, the surface is lit by one of the lights, picked among four, with a shadow ray, and reflects what one more ray reads from the cache; where it leaves, the sky shows. The bounce has the shadows and detail the cache's cells miss, and follows a moving light, a flashlight, at once. `renderer.gi_bounces` lights more surfaces before the cache is read, or none, and `renderer.gi_rays` casts more rays per pixel. Bounced light is clamped in luminance (`renderer.gi_clamp`), so that a lamp against a wall does not spread fireflies; direct light never is. Cells are kept apart by the orientation of their surface, and by an anchor that a point behind a wall cannot see: light does not leak through walls. Each pixel also casts a ray along its specular lobe (ADR-0042), drawn from the GGX distribution of its roughness: on smooth surfaces it shades what it hits, a mirror reflecting on up to three surfaces; on rougher ones it reads the cache where it hits, and fills cells no other path goes to; above `renderer.reflection_roughness` (0.6), no ray is cast and the indirect light stands in. The lighting multiplies what came back by the specular albedo. A sharp reflection shows emissive lights, whose specular light light sampling then leaves out, so each light is counted once. Glass (`KHR_materials_transmission`, with its IOR and volume) is left out of the G-buffer: after particles, the frame is copied, and the glass pass draws glass over it, testing the G-buffer's depth itself. A pixel of glass is its reflection, as much as Fresnel says, plus what goes through, tinted: the frame behind it for thin glass; for thick glass, a ray bent in, across to the far side and bent out, absorbed on the way. What those rays find is read from the frame when it is on screen, and shaded otherwise. Glass casts no shadow, and only the glass pass and the path tracer see it. Every light casts shadows: the shadow ray goes against the frame's acceleration structure, and alpha-tested surfaces let light through their holes (ADR-0035). A light is a sphere, 5 cm across by default for `scene::Light`: each frame, the ray aims at another point of it, so shadows are soft and noisy at their edges. The lighting writes the diffuse and specular light apart from the emission, without the surface's colors, and the denoiser averages them with their history where the surface is the same as last frame, then with neighbours on the same surface, as much as their noise asks: light that is not noisy, such as a hard shadow, goes through unchanged. On smooth surfaces, the specular history is found where the reflected point was, from the reflection's hit distance. A composite pass multiplies the colors back in and adds the fog (ADR-0039). Where nothing is drawn, it shows the level's sky, a cube texture (ADR-0036). Nothing beyond the view distance is drawn: the projection has a far plane there. Edges are smoothed by temporal antialiasing (ADR-0037): each frame moves the camera by a fraction of a pixel, the G-buffer writes where each pixel's surface was in the previous frame, from the previous matrices that `ExtractScene` keeps per entity, and a compute pass blends the frame with the history of the earlier ones, clipped towards the colors around each pixel. Particles mark what they cover, where the history counts less, and a teleport or a new scene starts the history over. Draws can opt out, as the sandbox's weapons do. Each group of passes is timed with GPU timestamps, which `Renderer::GpuTimes` returns and the developer tools show.

`MeshPass` keeps one frame slot per frame in flight, so the CPU never writes data the GPU is still reading. Shaders are written in Slang, compiled to SPIR-V by `slangc` during the build and embedded in the binary. In development builds, the sources are watched and recompiled by the `slangc` executable (ADR-0031).

## The render graph

Each frame, the renderer and its passes add their passes to a `RenderGraph`, in the order they run (ADR-0032). A pass declares the textures and buffers it uses and, if it renders, its targets; the graph places the barriers between passes, wraps each in a debug label, and times each group of passes. It does not reorder passes or own resources.

```mermaid
flowchart LR
    SK["Skinning"] --> AS["Acceleration<br/>structures"] --> EL["Emissive<br/>lights"] --> PP["G-buffer"] --> GI["Radiance cache<br/>update, resolve"] --> IL["Indirect light<br/>one ray per pixel"] --> SI["Simulation<br/>emit, simulate,<br/>finish, sort"] --> ME["Direct lighting<br/>candidates, temporal,<br/>spatial, shading"] --> DN["Denoiser<br/>temporal, 5 spatial"] --> CO["Composite"] --> PA["Particles"] --> TR["Temporal<br/>resolve"] --> TO["Tonemap"] --> UI["Interface"] --> OV["Overlay"]
```

Barriers come from the RHI: each texture and buffer remembers its last write and the reads since, and `CommandList::Use` decides from that whether the next use must wait (`rhi::NextAccess`). The GPU tests run with synchronization validation, which reports any use that no barrier orders.

## Acceleration structures

Rays are traced against a top-level acceleration structure rebuilt every frame from the frame's draws (ADR-0033). Each mesh has a bottom-level structure, built once with its upload. Skinned draws are skinned once per frame by a compute pass, into world-space vertices that the G-buffer draws and that each skinned draw's own structure is refitted from, so rays and rasterization see the same surface. A ray's hit names its draw's entry in the frame's instance data, which holds its vertices, indices and material. `renderer.debug_view rays` shows the structure from the camera, one color per instance.

## The reference path tracer

`renderer.debug_view path` replaces the lit scene with the reference path tracer (ADR-0034): a compute pass that traces one path per pixel per frame against the frame's acceleration structure and averages them while the view stays. It shades hits with the same materials, lights and BSDF as the lighting, and rays that leave the scene find the same sky; the BSDF lives in one Slang module, `Bsdf.slang`, that both import. It is what the ray-traced techniques of Phase 9 are compared with; it reflects off and goes through glass as Fresnel says. `sandbox.scene cornell` builds its test scene, the Cornell box, `sandbox.scene mirrors` the mirror room, of reflections and glass, and `sandbox.scene corridor` the dark corridor, lit only by the flashlight, of indirect light (ADR-0045).

## The game's interface

Games draw their HUD and menus on `Application::Canvas()`, a `ui::Canvas` begun again every frame (ADR-0027). It is immediate mode: rectangles, images and text are drawn in the order of the calls, in units of a screen 1080 high, and the game keeps its own state.

```mermaid
flowchart LR
    TTF["TrueType font"] -->|"resources::BakeFont<br/>stb_truetype"| FD["FontData<br/>distance field atlas,<br/>glyphs, kerning"]
    FD -->|"AssetLibrary::LoadFont"| F["ui::Font"]
    F --> C["ui::Canvas<br/>vertices, commands"]
    G["Game code<br/>FillRect, DrawImage, DrawText"] --> C
    C --> U["UiPass<br/>after tonemapping,<br/>before the overlay"]
    U --> S["Swapchain<br/>sRGB"]
```

Fonts are baked into signed distance fields when they load, so text stays sharp at any size. The interface draws after tonemapping and antialiasing, so that the temporal history never holds text, and under the debug overlay.

Menus use `ui::Context`, immediate mode too: buttons, toggles, sliders and choices draw themselves on the canvas and return whether the mouse or the keyboard used them, and the game keeps the values (ADR-0029). The context only remembers which widget has the focus, by its place in the frame. Behind a menu, `Scene::SetPaused` stops the scene's systems, physics, animations, particles and sounds, and the scene is still drawn as it was.

## Developer tools

Development builds have tools on top of the Dear ImGui overlay, opened with the key under Escape (ADR-0030). While they are open, the pointer is free and the game gets no keyboard or mouse input; the game itself keeps running.

```mermaid
flowchart LR
    E["Platform events<br/>keys, mouse, text"] --> T{"Tools open?"}
    T -->|"no"| I["InputState<br/>the game"]
    T -->|"yes"| O["ImGui overlay"]
    E --> O
    O --> C["Console<br/>log, cvars, commands"]
    O --> S["Statistics<br/>frame and GPU times,<br/>Renderer::Stats, video memory"]
    O --> N["Entities<br/>hierarchy and inspectors"]
    O --> R["Resources<br/>AssetLibrary::Describe"]
```

The console runs a command, shows a cvar, or sets one for this run; its log is a sink that keeps the last lines from every thread. Components have no reflection: each type the inspector shows has a small function that draws it, and games add their own components, windows and commands.

## Particles

Particles live on the GPU, in one pool of 16,384 (ADR-0025). The CPU only sends the frame's spawn requests: `scene::ParticleEmitter` entities emit at a steady rate, and `Scene::SpawnParticles` adds bursts.

```mermaid
flowchart LR
    E["ParticleEmitter entities,<br/>Scene::SpawnParticles"] -->|"Extract"| V["SceneView::particleSpawns"]
    V --> EM["Emit<br/>slots from the dead list"]
    EM --> SI["Simulate<br/>gravity, drag, bounces<br/>off the G-buffer, light"]
    SI --> FI["Finish<br/>draw arguments"]
    FI --> SO["Bitonic sort<br/>back to front"]
    SO --> D["Indirect draw<br/>soft, premultiplied"]
```

Effects are created on the Renderer from a `ParticleEffectDesc`; `AssetLibrary::CreateParticleEffect` owns them with the level's other assets. Particles hide behind meshes and fade into them, lit ones take the light the simulation keeps for each of them: each particle keeps the light it chose, as ReSTIR does for pixels, tested for shadows, and additive ones glow in the same draw as the smoke.

## Tests

| Location | What it covers | Needs a GPU |
|---|---|---|
| `tests/unit/<module>/` | Logic of one module, one executable per module | No |
| `tests/gpu/<module>/` | `rhi`, `renderer` (with the debug overlay) and `scene` on a real driver, with no mock device, under validation and synchronization validation | Yes, or lavapipe on Linux |

CI runs one job on pull requests (ADR-0014): the formatting check, then Linux Clang Debug with AddressSanitizer and UBSan, with all GPU tests on lavapipe under a virtual display. Pull requests that only change documentation skip it. Windows MSVC (Debug, Release, ASan, Profile) is built and tested locally.

A tag `vX.Y.Z` runs the Release workflow (ADR-0038): it builds the `*-shipping` presets on Windows and Linux, packages each with CPack, and creates a draft GitHub release that is published by hand.

## Known gaps

- No tool produces KTX2 files yet, although the loader reads them.
- Particles only collide with what is on screen.
- Particles are lit evenly from every side, and cast no shadow: their indirect light is that of the surfaces behind them.
- What the cache holds lags a few frames behind a moving light, and starts over after a cut while the cache fills: light that bounced more than once, since the first bounce is lit at once.
- Indirect light, the cache and the bounce, takes 1.5 ms at 1080p on an RTX 3090 in the factory (ADR-0045).
- The factory is lit only where its lamps are: the route has 13 of them since ADR-0040, its glowing door lights light their doorways (ADR-0044), and the underground stays dark until the flashlight is found.
- ReSTIR DI takes 1.1 ms at 1080p on an RTX 3090 in the factory and 2.2 ms in a room of 1,000 lights, over its budget of 1 ms (ADR-0044).
- The denoiser reprojects the highlights of lights with the surface's motion: one that moves across a surface as the camera turns lags behind until the clamp to its neighbours catches it. Reflections have a reprojection of their own on smooth surfaces.
- The view distance cuts the scene sharply where fog does not hide it, and a wide field of view stretches the weapon held in front of the camera, which has no field of view of its own.
- Reflections show no glass, and shadows and indirect light go through glass untinted. Glass is smooth, and blurred reflections of metals are dark: the cache holds diffuse light only.
- Sound passes through walls and doors: there is no occlusion.
- Volumes are read at startup and cannot change during play.
- Joint matrices are computed and uploaded for every skinned mesh, even out of view, and collisions use the bind pose of skinned meshes.
- Nothing culls: every `DrawItem` is drawn.
- There are no text input events, so ImGui text fields do not work.
- Creating a model's GPU objects still happens on the main thread: about 10 ms for the Factory level in Release, which is less than a frame at 60 Hz but more than one at 144 Hz.
- Levels can only carry gameplay data in the names of glTF nodes; their lights come in `KHR_lights_punctual`.
- A change of exposure leaves the cache and the denoiser's histories off by its ratio for a few frames.
