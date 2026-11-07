# Architecture

How the engine is built today, through Phase 9.6: real lights with ray-traced shadows, a sky, clustered lights, ambient occlusion, PBR materials, normal maps and particles on the GPU. [OVERVIEW.md](../OVERVIEW.md) holds the vision and the roadmap, and [the ADRs](adr/README.md) hold the reasons for each decision. This page describes what the code does now, and it changes in the same pull request as the code.

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
| `renderer` | Draws a `SceneView`: mesh pass, skinning, materials, shader hot reload, the game's interface (`ui::`), and the debug overlay (`debugui::`, development builds) | Dear ImGui, stb_truetype (in `resources`) |
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
    A->>S: Extract: transforms, SceneView with joint matrices, particle spawns, sounds and listener
    A->>G: OnRender
    A->>R: Render(SceneView, overlay)
    R->>R: Reload changed shaders
    R->>D: BeginFrame: send the pending uploads, read last timestamps
    R->>D: Skinning, then the frame's acceleration structures
    R->>D: Pre-pass: depth and normals
    R->>D: Ambient occlusion, at half resolution
    R->>D: Particles: emit, simulate, sort (compute)
    R->>D: Mesh pass into the HDR target, where depth is equal
    R->>D: Particles drawn back to front, one indirect draw
    R->>D: Tonemap into the back buffer, or through FXAA, then overlay
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
    P["Push constants<br/>buffer and element indices"] --> FB["FrameData<br/>view-projection, fog,<br/>ambient, sky"]
    FB --> LB["LightData<br/>every light of the frame"]
    P --> IB["InstanceData<br/>world matrix, vertices,<br/>indices, material"]
    IB --> VB["Vertices and indices<br/>of the mesh, or its<br/>skinned vertices"]
    P --> MB["MaterialData<br/>factors and texture indices"]
    MB --> T["Bindless texture array"]
    FB --> AS["The frame's acceleration<br/>structure, by index"]
    FB --> CT["Bindless cube array<br/>the sky"]
```

## Lighting

Meshes are shaded in linear HDR with metallic-roughness PBR and the frame's lights, then tonemapped (ADR-0022). Lights are `scene::Light` entities: a spot light shines down its entity's local -Z, so a lamp is placed and turned like anything else, and a light parented to the camera follows it.

```mermaid
flowchart LR
    E["scene::Light<br/>on entities"] -->|"ExtractScene"| L["SceneView::lights<br/>position, direction, color, range"]
    AS["Acceleration structure<br/>of the frame"] -->|"a shadow ray<br/>per light"| M
    L -->|"CPU clustering<br/>16×9×24"| CL["Cluster light lists"]
    CL --> M["Mesh pass<br/>GGX, normal maps,<br/>shadows, ambient, fog,<br/>then the sky"]
    P["Pre-pass<br/>depth, normals"] --> AO["Ambient occlusion<br/>half resolution"]
    AO --> M
    M --> H["HDR target<br/>Rgba16Float"]
    H -->|"exposure,<br/>PBR Neutral"| S["Swapchain<br/>sRGB"]
```

A light's color is what a white surface facing it reflects up close, and it fades linearly to zero at its range, as in the legacy engine. Normal maps follow glTF (green up, bitangent from the tangent's `w`), and meshes that lack tangents get MikkTSpace ones at load. Each frame, lights are assigned to 16×9×24 clusters of the view on the CPU, and a pixel shades only the lights of its cluster (ADR-0008). A pre-pass writes depth and normals first, so that the mesh pass shades each pixel once, and ambient occlusion darkens the ambient light in creases (ADR-0024). The ambient light itself comes more from above than from below. Every light casts shadows: for each light of its cluster that would light it, a pixel casts a ray towards the light against the frame's acceleration structure, and alpha-tested surfaces let light through their holes (ADR-0035). Where nothing is drawn, the mesh pass ends with the level's sky, a cube texture, behind everything (ADR-0036). Nothing beyond the view distance is drawn or lit: the projection has a far plane there, and the clusters end at it. Edges are smoothed by FXAA after tonemapping, or by multisampling the pre-pass and the mesh pass, which resolve into the targets the later passes read (ADR-0026). Draws can opt out, as the sandbox's weapons do. Each group of passes is timed with GPU timestamps, which `Renderer::GpuTimes` returns and the developer tools show.

`MeshPass` keeps one frame slot per frame in flight, so the CPU never writes data the GPU is still reading. Shaders are written in Slang, compiled to SPIR-V by `slangc` during the build and embedded in the binary. In development builds, the sources are watched and recompiled by the `slangc` executable (ADR-0031).

## The render graph

Each frame, the renderer and its passes add their passes to a `RenderGraph`, in the order they run (ADR-0032). A pass declares the textures and buffers it uses and, if it renders, its targets; the graph places the barriers between passes, wraps each in a debug label, and times each group of passes. It does not reorder passes or own resources.

```mermaid
flowchart LR
    SK["Skinning"] --> AS["Acceleration<br/>structures"] --> PP["Prepass"] --> OC["Occlusion<br/>2 passes"] --> SI["Simulation<br/>emit, simulate,<br/>finish, sort"] --> ME["Meshes"] --> PA["Particles"] --> TO["Tonemap"] --> AA["Antialiasing"] --> UI["Interface"] --> OV["Overlay"]
```

Barriers come from the RHI: each texture and buffer remembers its last write and the reads since, and `CommandList::Use` decides from that whether the next use must wait (`rhi::NextAccess`). The GPU tests run with synchronization validation, which reports any use that no barrier orders.

## Acceleration structures

Rays are traced against a top-level acceleration structure rebuilt every frame from the frame's draws (ADR-0033). Each mesh has a bottom-level structure, built once with its upload. Skinned draws are skinned once per frame by a compute pass, into world-space vertices that the pre-pass and the mesh pass draw and that each skinned draw's own structure is refitted from, so rays and rasterization see the same surface. A ray's hit names its draw's entry in the frame's instance data, which holds its vertices, indices and material. `renderer.debug_view rays` shows the structure from the camera, one color per instance.

## The reference path tracer

`renderer.debug_view path` replaces the lit scene with the reference path tracer (ADR-0034): a compute pass that traces one path per pixel per frame against the frame's acceleration structure and averages them while the view stays. It shades hits with the same materials, lights and BSDF as the mesh pass, and rays that leave the scene find the same sky; the BSDF lives in one Slang module, `Bsdf.slang`, that both import. It is what the ray-traced techniques of Phase 9 are compared with. `sandbox.scene cornell` builds its test scene, the Cornell box.

## The game's interface

Games draw their HUD and menus on `Application::Canvas()`, a `ui::Canvas` begun again every frame (ADR-0027). It is immediate mode: rectangles, images and text are drawn in the order of the calls, in units of a screen 1080 high, and the game keeps its own state.

```mermaid
flowchart LR
    TTF["TrueType font"] -->|"resources::BakeFont<br/>stb_truetype"| FD["FontData<br/>distance field atlas,<br/>glyphs, kerning"]
    FD -->|"AssetLibrary::LoadFont"| F["ui::Font"]
    F --> C["ui::Canvas<br/>vertices, commands"]
    G["Game code<br/>FillRect, DrawImage, DrawText"] --> C
    C --> U["UiPass<br/>after FXAA,<br/>before the overlay"]
    U --> S["Swapchain<br/>sRGB"]
```

Fonts are baked into signed distance fields when they load, so text stays sharp at any size. The interface draws after tonemapping and antialiasing, so that FXAA does not blur text, and under the debug overlay.

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
    EM --> SI["Simulate<br/>gravity, drag, bounces<br/>off the pre-pass"]
    SI --> FI["Finish<br/>draw arguments"]
    FI --> SO["Bitonic sort<br/>back to front"]
    SO --> D["Indirect draw<br/>soft, lit by clusters,<br/>premultiplied"]
```

Effects are created on the Renderer from a `ParticleEffectDesc`; `AssetLibrary::CreateParticleEffect` owns them with the level's other assets. Particles hide behind meshes and fade into them, lit ones take the light of their cluster, and additive ones glow in the same draw as the smoke.

## Tests

| Location | What it covers | Needs a GPU |
|---|---|---|
| `tests/unit/<module>/` | Logic of one module, one executable per module | No |
| `tests/gpu/<module>/` | `rhi`, `renderer` (with the debug overlay) and `scene` on a real driver, with no mock device, under validation and synchronization validation | Yes, or lavapipe on Linux |

CI runs one job on pull requests (ADR-0014): the formatting check, then Linux Clang Debug with AddressSanitizer and UBSan, with all GPU tests on lavapipe under a virtual display. Pull requests that only change documentation skip it. Windows MSVC (Debug, Release, ASan, Profile) is built and tested locally.

## Known gaps

- No tool produces KTX2 files yet, although the loader reads them.
- Ambient occlusion only sees what is on screen, and particles only collide with what is on screen.
- Particles are lit evenly from every side, and they cast no shadow and receive none.
- Shadows are hard: a point or spot light has no size yet, and soft shadows wait for the denoiser.
- The view distance cuts the scene sharply where fog does not hide it, and a wide field of view stretches the weapon held in front of the camera, which has no field of view of its own.
- With multisampling, alpha-tested materials are not smoothed: there is no alpha to coverage.
- There are no reflections: metals reflect only an estimate of the ambient light.
- Sound passes through walls and doors: there is no occlusion.
- Volumes are read at startup and cannot change during play.
- Joint matrices are computed and uploaded for every skinned mesh, even out of view, and collisions use the bind pose of skinned meshes.
- Nothing culls: every `DrawItem` is drawn.
- There are no text input events, so ImGui text fields do not work.
- Creating a model's GPU objects still happens on the main thread: about 10 ms for the Factory level in Release, which is less than a frame at 60 Hz but more than one at 144 Hz.
- Levels can only carry gameplay data in the names of glTF nodes.
