# Daidalos Engine Next — Project Knowledge Base

**Document type:** Project vision, architecture, technical requirements, development guidelines, and roadmap  
**Project name:** Daidalos Engine Next  
**Primary language:** C++  
**Initial target platform:** Windows  
**Long-term target platforms:** Windows and Unix-like operating systems, including Linux  
**Development environment:** Visual Studio 2022 and CMake, with cross-platform build support planned  
**Rendering API:** Vulkan 1.4 with mandatory hardware ray tracing, on Windows and Linux (see ADR-0007, ADR-0028, ADR-0031)  
**Language standard:** C++20 (see ADR-0001)  
**Licence:** MIT or Apache 2.0, at your option (see ADR-0047)  
**Repository:** https://github.com/StephaneBX/daidalos-engine-next  
**Decision records:** `docs/adr/`; coding and math conventions: `docs/CONVENTIONS.md`  
**Development approach:** Modular architecture, incremental implementation, testable components, and AI-assisted development  
**Current project status:** Phases 0 to 8 (foundation to modern rendering, the game's interface, menus and developer tools) are implemented; Phase 9 moves rendering to ray tracing (ADR-0028); the engine technical spec for a Doom-inspired game with an oppressive horror atmosphere sets the phases after it (ADR-0041); implementation proceeds through small, verifiable milestones, each of which improves the demo described in `docs/DEMO.md`

---

# 1. Project Overview

## 1.1. Vision

Daidalos Engine Next is a custom C++ real-time 3D engine intended primarily for indoor environments and first-person or third-person game experiences.

The objective is to build a reusable, modular, maintainable engine that provides the essential systems required to develop complete 3D games without relying on a commercial engine such as Unreal Engine or Unity.

The engine is intended to provide direct control over rendering, resource management, entity organization, input, physics, audio, gameplay integration, and development tools.

Rather than attempting to reproduce every feature of a general-purpose commercial engine, Daidalos Engine Next should focus on a clearly defined feature set and a manageable development scope.

The engine should favor established libraries for well-understood problems while retaining ownership of its core architecture and high-level behavior.

The project is also an opportunity to develop and maintain a deep understanding of modern C++ engine architecture, real-time rendering, graphics APIs, resource lifetime management, and cross-platform software design.

## 1.2. Core objectives

The project has the following main objectives:

1. Build a modular C++ engine with clear architectural boundaries.
2. Render through a thin rendering abstraction layer (RHI) over Vulkan 1.3, keeping a second backend possible but not planned.
3. Implement a modern clustered forward rendering pipeline with physically based rendering, normal mapping, and ambient occlusion.
4. Provide an Entity-Component-System architecture with entity hierarchy and transform propagation.
5. Implement reliable asset loading, resource caching, and resource lifetime management.
6. Support keyboard, mouse, and gamepad input.
7. Integrate audio, physics, AI, gameplay logic, and cinematic camera functionality.
8. Provide a reusable 2D GUI system for menus, HUDs, cursors, loading screens, and other in-game interfaces.
9. Provide development and debugging tools, including an editor or editor-like application.
10. Keep the architecture portable enough to support Windows and Unix-like platforms.
11. Make the project maintainable by a single developer.
12. Use Claude Code as a development assistant without sacrificing correctness, architectural understanding, or code quality.

## 1.3. Guiding principles

The following principles should guide all technical decisions.

### Modularity

Each subsystem must have a clear responsibility and a limited public interface.

A module should be replaceable or testable without requiring unrelated engine systems to be rewritten.

### Explicit dependencies

Dependencies should be visible in constructors, interfaces, or module initialization code.

Avoid unnecessary global state, hidden dependencies, and universal service locators.

### Platform independence

Platform-specific functionality must be isolated behind well-defined interfaces.

The majority of the engine should not depend directly on Windows APIs, SDL types, Vulkan types, or other platform-specific details.

### Incremental development

Implement and validate one vertical slice at a time.

A small, working engine is more valuable than a large collection of unfinished abstractions.

### Simplicity before generality

Do not build a general-purpose framework unless a concrete engine requirement justifies it.

Prefer the simplest design that satisfies current requirements while allowing reasonable future extension.

### Ownership and lifetime safety

Resource ownership, object lifetime, thread ownership, and cleanup responsibilities must be explicit.

Raw pointers may be used for non-owning references where appropriate, but ownership must be clear and enforceable.

### Testability

Core logic should be testable without creating a graphics device or launching a complete game whenever possible.

### Maintainability

Readable, conventional C++ is preferred over clever abstractions or premature optimization.

### Measurable progress

Every substantial milestone should produce a runnable, testable result.

---

# 2. Project Scope

## 2.1. In scope

The intended engine includes:

- A platform abstraction layer.
- Window creation and application lifecycle management.
- Keyboard, mouse, and gamepad input.
- Logging and diagnostics.
- A mathematical foundation.
- An Entity-Component-System architecture.
- Entity hierarchy and transform propagation.
- A rendering hardware interface.
- A Vulkan 1.3 rendering backend running on Windows and Linux.
- A modern 3D rendering pipeline.
- Mesh, material, texture, shader, and other asset loading.
- Resource caching and lifetime management.
- Scene and world organization.
- Physics integration.
- Audio integration.
- Gameplay integration.
- AI functionality.
- Cinematic camera functionality.
- A 2D user interface for games.
- Debugging and profiling support.
- Automated tests.
- Development tools and an editor application.
- Cross-platform build infrastructure.

## 2.2. Out of scope unless explicitly approved

The engine should not automatically expand to include the following features:

- A full commercial-grade editor comparable to Unreal Editor.
- A complete visual scripting language.
- A proprietary physics solver implemented from scratch.
- A proprietary audio engine implemented from scratch.
- A complete networking and multiplayer framework.
- An MMORPG-scale world architecture.
- Large-scale outdoor world streaming and terrain systems.
- A general-purpose cinematic production suite.
- A custom programming language.
- An extensive plugin marketplace.
- Support for every operating system and graphics API.
- A universal rendering architecture designed to accommodate every possible future backend.
- A fully general-purpose asset pipeline before a real game requires it.

These features may be reconsidered later, but they must not delay the first working engine.

## 2.3. Indoor-environment specialization

The initial engine is designed primarily for indoor environments.

Examples include:

- Rooms and corridors.
- Buildings and connected interiors.
- Small to medium-sized environments.
- Environments with walls, doors, portals, and constrained visibility.
- Environments where visibility determination can benefit from spatial partitioning.

This specialization may enable optimizations such as:

- Frustum culling.
- Spatial partitioning.
- Portal-based visibility.
- Bounding volume hierarchies for static geometry.
- Static geometry batching.
- Efficient indoor lighting strategies.

However, these techniques must be introduced only when justified by the scene representation and measured performance.

Portal rendering should be considered an optional specialized system, introduced only if a representative test level shows a measured benefit. BSP is not planned (see ADR-0012).

---

# 3. Development Constraints

## 3.1. Individual development

The project is expected to be developed primarily by one person.

This has major architectural consequences.

The engine must prioritize:

1. Small implementation steps.
2. Minimal maintenance overhead.
3. Reuse of mature third-party libraries.
4. Simple build and debugging workflows.
5. Automated validation.
6. Documentation of architectural decisions.
7. AI-assisted implementation with human review.

Avoid introducing technologies that require extensive maintenance, complex build pipelines, or a large amount of infrastructure without a concrete benefit.

## 3.2. AI-assisted development

Claude Code may assist with:

- Repository exploration.
- Code generation.
- Refactoring.
- Implementation planning.
- Unit tests.
- Build configuration.
- Debugging.
- Documentation.
- Code reviews.
- Identifying missing error handling.
- Comparing implementation alternatives.

Claude Code must not assume that generated code is correct simply because it compiles.

Every significant implementation should be checked against the architecture, built where possible, and tested.

The developer remains responsible for architectural decisions and must be able to understand and maintain the resulting code.

## 3.3. Scope management

The full engine is a long-term project.

The recommended strategy is to establish a small, complete, working foundation before implementing advanced systems.

The first meaningful result should be a running application that creates a window, initializes the rendering backend, renders a simple 3D scene, and shuts down cleanly.

Advanced rendering, AI, physics, audio, and editor functionality should be added progressively.

---

# 4. Target Platforms and Build System

## 4.1. Platforms

### Initial platform: Windows

Windows is the initial development and validation platform.

Visual Studio 2022 is the primary IDE.

Vulkan 1.3 is the rendering API on Windows as well as Linux. DirectX 12 is not planned; the RHI keeps it possible if a concrete need appears (see ADR-0007).

### Long-term platform: Linux and other Unix-like environments

The architecture should avoid unnecessary Windows-specific assumptions.

Linux support is provided by SDL3 and the same Vulkan backend, and is validated continuously by CI.

Support for macOS or additional Unix-like environments should not be assumed until their requirements and rendering API constraints have been evaluated.

## 4.2. Build system

CMake is the build system (ADR-0001). It supports cross-platform project configuration and integrates with Visual Studio and common C++ toolchains.

Configurations are defined with `CMakePresets.json`. Third-party dependencies are managed with vcpkg in manifest mode (`vcpkg.json` with a pinned baseline). The former Visual Studio solution and NuGet packages are retired.

Continuous integration runs on GitHub Actions (one Linux Clang job; Windows MSVC is built and tested locally), and every build must also be reproducible locally with the same presets (ADR-0014).

The repository should eventually support:

- Debug and Release configurations.
- Clear separation of engine modules and applications.
- Automated test targets.
- Third-party dependency management.
- Reproducible builds.
- Platform-specific configuration where required.
- Continuous integration when the project reaches a suitable level of maturity.

The build system should remain straightforward.

Do not introduce elaborate build-generation scripts or custom package-management infrastructure unless CMake and the selected dependency-management approach prove insufficient.

## 4.3. Compiler and language standard

The engine should use modern, idiomatic C++.

The language standard is C++20 (ADR-0001), set explicitly in the build configuration.

Use modern language features when they improve clarity or safety, including:

- RAII.
- Smart pointers.
- Move semantics.
- `enum class`.
- `std::optional`.
- `std::variant`.
- `std::span`.
- Type-safe interfaces.
- Compile-time constants and constraints where appropriate.

Avoid using advanced language features merely because they are available.

C++ modules should not be a prerequisite for the initial architecture. Their adoption can be reconsidered after the toolchain and build workflow are stable.

---

# 5. High-Level Architecture

## 5.1. Architectural model

The engine should be divided into independent modules with explicit dependencies.

A conceptual module structure is:

```text
DaidalosEngine/
├── Core/
├── Platform/
├── Logging/
├── Math/
├── ECS/
├── Resources/
├── RHI/
├── Renderer/
├── Physics/
├── Audio/
├── Input/
├── GUI/
├── AI/
├── Gameplay/
├── Scene/
├── Application/
├── Tools/
├── Tests/
└── Samples/
```

This is a logical structure, not a requirement to create every directory immediately.

Modules should be introduced when there is enough functionality to justify them.

## 5.2. Module responsibilities

### Core

Provides fundamental engine-level utilities and shared contracts.

Potential responsibilities:

- Common types.
- Error and result conventions.
- Assertions.
- Fundamental lifecycle interfaces.
- Module initialization contracts.
- Small utilities genuinely shared by several modules.

Core must remain small.

It must not become a repository for unrelated features or a dependency that every module uses to access every other module.

### Platform

Encapsulates operating-system-specific behavior.

Responsibilities include:

- Application initialization.
- Window creation and destruction.
- Event processing.
- Platform-specific input integration.
- Timing and platform services where required.
- Platform-specific file-system behavior when a portable abstraction is needed.

The rest of the engine should depend on platform interfaces rather than directly on operating-system APIs.

SDL3 is the platform layer (ADR-0004). It is wrapped by concrete engine classes (for example `Window` and `EventQueue`) that hide SDL behind a private implementation. No additional `IPlatform` virtual interface is introduced, because SDL3 is already the portability layer.

### Logging

Provides structured logging and diagnostic output.

Logging should support different severity levels and enough context to identify failures.

The logging implementation should be replaceable without changing application-level logging calls throughout the engine.

### Math

Provides the mathematical primitives required by the engine.

Responsibilities include:

- Vectors.
- Matrices.
- Quaternions.
- Transform operations.
- Coordinate-space conversions.
- Geometric helpers.
- Bounding volumes.
- Camera-related mathematics.

The engine should use one consistent convention for handedness, vector orientation, matrix layout, and transform composition.

The convention must be documented and consistently respected by the renderer, camera, physics integration, and asset pipeline.

### ECS

Provides entity, component, system, and hierarchy management.

It should not depend directly on the graphics API or concrete gameplay implementations.

### Resources

Manages asset loading, resource identification, caching, and lifetime.

It should be possible to load and inspect many resource types without coupling the resource system to a specific renderer implementation.

### RHI

Provides the rendering hardware interface.

It defines the operations and resource abstractions required by the renderer without exposing Vulkan types. It isolates Vulkan from the renderer and keeps a future backend possible. It is made of concrete classes, tested on a real Vulkan driver (ADR-0007).

### Renderer

Implements rendering policies and the high-level rendering pipeline.

It coordinates scene rendering, materials, lighting, visibility, cameras, and rendering passes.

The renderer should use the RHI rather than issuing backend-specific calls directly.

### Physics

Provides physics integration and the interfaces required to synchronize physical state with entities and transforms.

### Audio

Provides audio playback, spatial audio, sound emitters, listeners, and resource integration.

### Input

Provides a consistent interface for keyboard, mouse, and gamepad input.

Input should support both low-level state queries and a higher-level action mapping system if the implementation requires it.

### GUI

Provides in-game user interface functionality, including menus, cursors, HUDs, loading screens, and interactive widgets.

Developer tools may use a separate immediate-mode GUI library.

### AI

Provides reusable AI functionality appropriate to the intended games.

Initial systems may include finite-state machines, behavior logic, navigation queries, and perception abstractions.

### Gameplay

Provides game-specific logic and orchestration.

Gameplay code should use engine interfaces rather than modifying internal renderer, platform, or resource implementation details.

### Scene

Organizes world instances, entity hierarchies, scene loading, and scene transitions.

Scene should integrate with ECS and Resources without becoming a duplicate of either.

### Application

Provides the executable entry point and coordinates engine startup, update, rendering, and shutdown.

### Tools

Contains development utilities, debugging interfaces, asset inspection, and the editor application.

### Tests

Contains automated tests for isolated systems and integration behavior.

### Samples

Contains small applications and sample scenes used to validate engine capabilities.

## 5.3. Dependency rules

The architecture must enforce the following principles:

1. Low-level modules must not depend on high-level gameplay systems.
2. ECS must not depend directly on Vulkan or any graphics API.
3. Renderer policies must not depend directly on operating-system-specific APIs.
4. Platform-specific code must be isolated.
5. Resource loaders must not introduce circular dependencies between Resources and Renderer.
6. The editor must use public engine interfaces wherever practical.
7. Tests must be able to exercise isolated logic without requiring the full engine.
8. Dependencies must be explicit and justified.
9. A module must not be added to Core simply to avoid designing an appropriate dependency.
10. Circular dependencies must be treated as architectural defects.

A typical dependency direction is:

```text
Application
 ├── Platform
 ├── Core / Logging
 ├── ECS / Scene
 ├── Resources
 ├── Renderer
 │    └── RHI
 ├── Input
 ├── Physics
 ├── Audio
 ├── GUI
 └── Gameplay / AI

Tools
 └── Public engine interfaces
```

This diagram is conceptual. The exact dependency graph must be validated during implementation, especially for shared types, resource handles, and initialization contracts.

---

# 6. Engine Lifecycle and Application Architecture

The application should have a clear and predictable lifecycle.

A conceptual startup sequence is:

1. Initialize logging.
2. Initialize platform services.
3. Create the application window.
4. Initialize input.
5. Create the rendering device and backend.
6. Initialize resource services.
7. Initialize ECS and scene management.
8. Initialize physics, audio, GUI, and other required systems.
9. Load the initial scene.
10. Enter the main loop.

The exact ordering must reflect actual dependencies.

A conceptual frame loop is:

```text
while application is running:
    process platform events
    update input state
    calculate frame timing
    update gameplay
    update AI
    update physics
    synchronize entity transforms
    prepare visible scene data
    execute rendering
    render GUI
    present frame
```

This is illustrative, not a mandatory final ordering.

Physics may require a fixed timestep, while rendering and input may operate at different frequencies. The update order must be designed to prevent inconsistent simulation state.

Shutdown must reverse initialization dependencies safely.

The engine must handle partial initialization failures without attempting to use uninitialized systems.

Avoid global initialization and destruction order dependencies.

---

# 7. Core C++ Design and Resource Ownership

## 7.1. RAII

Resource acquisition and release should follow RAII principles.

Objects responsible for operating-system handles, graphics resources, file handles, and other owned resources must have clear cleanup behavior.

Use smart pointers where they represent actual ownership.

Do not use `std::shared_ptr` everywhere by default. Shared ownership should be used only when multiple parties genuinely share lifetime responsibility.

## 7.2. Raw pointers and references

Raw pointers may represent non-owning relationships.

Their lifetime assumptions must be documented and enforced.

References should be used where null is not a valid state.

Use handles or IDs where they provide safer references to resources or entities that may outlive a particular C++ object.

## 7.3. Error handling

Engine operations that may fail should have an explicit error-reporting strategy.

Examples include:

- Window creation.
- Graphics device initialization.
- Shader compilation.
- Asset loading.
- Resource allocation.
- Scene loading.
- Backend initialization.
- External library initialization.

Errors should contain sufficient context to diagnose the problem.

Avoid silently ignoring failures.

Avoid using exceptions as an undocumented control-flow mechanism.

Decision (ADR-0003): operations that may fail return `Result<T>` (`tl::expected<T, Error>`), where `Error` carries a code, the module, and a message with context. Programming errors are caught with `DAI_ASSERT`. Exceptions remain enabled in the compiler because some third-party and standard-library code throws, but no exception may cross a module boundary: code that calls a throwing API catches at that boundary and converts to `Result`.

## 7.4. Threading

Multithreading may eventually be useful for asset loading, job execution, and rendering preparation.

However, a job system and complex synchronization architecture should not be introduced before a concrete workload requires them.

Initially, favor a clear single-threaded execution model unless a selected external library or platform requirement dictates otherwise.

Any later multithreaded design must document ownership, synchronization, thread affinity, and shutdown behavior.

Known concurrency that exists regardless of this rule (ADR-0011):

- The CPU and GPU run in parallel with several frames in flight, so GPU resources are destroyed through a deferred-destruction queue from the first renderer onward.
- The physics library requires a job system; its thread pool is accepted and confined to the physics module.
- A single worker pool decodes assets, so that the window stays responsive during loading (Phase 5). Results are always delivered on the main thread.

Game logic, ECS updates, and render submission remain single-threaded.

---

# 8. Entity-Component-System Architecture

## 8.1. Objectives

The ECS is a foundational engine subsystem.

It must allow entities to be assembled from reusable components and processed by systems without forcing gameplay objects into a rigid inheritance hierarchy.

The ECS should be independent of the rendering API and should not contain game-specific behavior.

## 8.2. Entities

Entities represent identities in the world.

They should be lightweight and should not require a dedicated subclass for each gameplay object.

Entity identifiers should have a defined validity model.

If entity IDs may be reused, generation counters or an equivalent mechanism should prevent stale handles from accidentally identifying newly created entities.

## 8.3. Components

Components hold data associated with entities.

Engine components (ADR-0006):

- LocalTransform (authoritative, parent-relative).
- WorldTransform (derived).
- Hierarchy.
- Camera.
- MeshRenderer (mesh and material references).
- Light.
- RigidBody and Collider.
- AudioEmitter and AudioListener.

Game components, such as Movement, Health, InputControl, AIController, and other gameplay-specific state, belong to game or sample code, not to the engine.

There is no separate Position component (it would duplicate the transform) and no Texture component (textures belong to materials).

Components should be added when required by actual use cases. Do not create redundant components that duplicate the same information.

Prefer data-oriented component structures where they improve iteration and system organization.

## 8.4. Systems

Systems operate on entities that possess the required component combinations.

Examples include:

- Transform propagation.
- Input processing.
- Movement.
- Physics synchronization.
- Rendering extraction.
- Audio listener and emitter updates.
- AI updates.
- Animation updates if animation is implemented.
- Gameplay state processing.

Systems should have explicit dependencies and should avoid hidden ordering assumptions.

## 8.5. Parent-child hierarchy

The engine must support entity hierarchy.

A child entity may be attached to a parent and inherit the parent's transform.

Examples include:

- A camera attached to a character.
- A weapon attached to a hand or character.
- A light attached to a moving object.
- A sound emitter attached to an entity.
- An object composed of multiple parts.

The hierarchy implementation must define:

- Parent references.
- Child references or an equivalent traversal mechanism.
- Local transforms.
- World transforms.
- Transform propagation.
- Reparenting behavior.
- Entity destruction behavior.
- Cycle prevention.
- Handling of invalid or destroyed parents.

Local transforms should be the authoritative representation of parent-relative placement.

World transforms should be derived and updated in a controlled manner.

The implementation must avoid stale world transforms when an ancestor changes.

## 8.6. Transform conventions

The engine must explicitly establish:

- Coordinate-system handedness.
- Up-axis.
- Forward-axis.
- Vector and matrix conventions.
- Matrix multiplication order.
- Quaternion convention.
- Transform composition order.
- World and local space semantics.

These conventions must be documented before integrating the renderer, physics engine, and external asset formats.

Decision (ADR-0005, details in `docs/CONVENTIONS.md`): GLM; right-handed; +Y up; −Z forward; +X right; column vectors; `world = parent * local`; local matrix `M = T * R * S`; quaternions `(w, x, y, z)` with unit length; meters, seconds, radians; clip-space depth in [0, 1] with reverse-Z (`GLM_FORCE_DEPTH_ZERO_TO_ONE`); Vulkan's downward clip-space Y is handled once with a negative viewport height. These match glTF 2.0, so imported assets need no axis conversion.

## 8.7. ECS validation

The ECS must eventually have automated tests covering:

- Entity creation and destruction.
- Component addition and removal.
- Component lookup.
- Invalid entity handling.
- Entity ID reuse.
- System iteration.
- Parent-child relationships.
- Reparenting.
- Transform propagation.
- Hierarchy cycle prevention.
- Destruction of parents and children.

The ECS should be validated independently of rendering.

---

# 9. Resource Management and Asset Loading

## 9.1. Objectives

The resource system must load, identify, cache, and release assets in a predictable way.

It should support different resource types while keeping resource-specific loading logic separate from the resource manager.

Potential resource types include:

- Meshes.
- Textures.
- Materials.
- Shaders.
- Audio clips.
- Scene data.
- Fonts.
- Other engine-specific asset types.

## 9.2. Loader architecture

Each resource type should have an appropriate loader interface.

Conceptually:

```text
ResourceManager
    ├── MeshLoader
    ├── TextureLoader
    ├── MaterialLoader
    ├── ShaderLoader
    ├── AudioLoader
    └── SceneLoader
```

Decided in ADR-0016: there is no loader registry and no resource manager. Typed functions decode files into plain CPU data, typed caches store values by identity, and `scene::AssetLibrary` turns them into GPU objects.

## 9.3. Resource identity

Resource identity should be stable and consistent.

The system must define how asset paths are normalized and how equivalent paths are recognized.

A resource should not be loaded multiple times unnecessarily because two different path spellings identify the same asset.

The treatment of case sensitivity must account for platform differences.

## 9.4. Caching and lifetime

The resource manager should avoid unnecessary duplicate resource loading.

It must also prevent resources from being destroyed while they are still in use.

Potential strategies include:

- Resource handles.
- Reference-counted ownership where appropriate.
- Explicit resource ownership by scenes or systems.
- Cache eviction policies introduced when necessary.

The chosen model is in ADR-0016: an asset library owns what it loads and releases it all together.

GPU resource lifetime requires particular care. Resources used by in-flight GPU work must not be destroyed before the graphics API permits their release.

## 9.5. CPU and GPU resources

The resource architecture should distinguish CPU-side asset data from GPU-side representations where appropriate.

For example:

```text
Asset file
    ↓
CPU-side resource data
    ↓
Renderer resource creation
    ↓
GPU-side representation
```

This separation can improve portability and testing.

It also prevents the generic resource manager from becoming unnecessarily coupled to Vulkan.

The exact abstraction should remain as simple as possible for the first working renderer.

## 9.6. Asynchronous loading

Asynchronous asset decoding uses a single worker pool (ADR-0011, ADR-0017).

The implementation defines:

- Worker-thread responsibilities.
- Completion notifications.
- Main-thread integration.
- Cancellation behavior.
- Resource ownership during loading.
- Error propagation.
- GPU upload synchronization.

---

# 10. Rendering Architecture

## 10.1. Objectives

The renderer must support modern real-time 3D rendering for indoor environments.

It should provide a clean separation between:

1. Scene data.
2. Rendering policies and passes.
3. The rendering hardware interface.
4. The concrete graphics API implementation.

The renderer should not expose Vulkan implementation details to gameplay code.

Sections 10.3 to 10.11 describe the rasterized renderer built in Phase 7. Phase 9 replaces its lighting with ray tracing, step by step (section 10.13, ADR-0028); each section is updated when its step lands.

## 10.2. Rendering hardware interface

The RHI is responsible for abstracting the low-level graphics operations needed by the engine.

Potential responsibilities include:

- Device creation.
- Swapchain and presentation integration.
- GPU resource creation.
- Buffers and textures.
- Shader and pipeline management.
- Command recording and submission.
- Render targets.
- Depth and stencil resources.
- Synchronization.
- Resource barriers or equivalent state transitions.
- Frame presentation.
- GPU resource destruction.

The interface should be designed around the actual requirements of the renderer rather than an attempt to abstract every feature of every graphics API.

The abstraction must not conceal Vulkan's explicit model so aggressively that it becomes difficult to implement correctly or efficiently.

API-specific functionality may be exposed through carefully designed capabilities or backend-specific internal interfaces when necessary.

## 10.3. Vulkan backend

Vulkan 1.4 is the single rendering backend, on Windows and Linux (ADR-0007, ADR-0031). It requires hardware ray tracing through ray queries, and uses dynamic rendering, synchronization2, and descriptor indexing, with the Vulkan Memory Allocator (VMA) for GPU memory.

The backend must explicitly handle:

- Instance, device, and queue creation.
- Command pools and command buffers.
- Descriptor management.
- Image layouts and pipeline barriers.
- Synchronization (one timeline semaphore, binary semaphores for the swapchain) with several frames in flight.
- Swapchain integration through SDL3.
- GPU memory and deferred resource destruction.
- Error reporting and device initialization failures.

These details must remain inside the backend or its private implementation layer. Validation layers are enabled in Debug builds, and a clean validation log is part of the definition of done for graphics work.

## 10.4. Additional backends

DirectX 12 or other backends are not planned. Vulkan already covers both target platforms, and a second backend would double the cost of every renderer feature.

The RHI remains thin enough that a second backend could be added later if a concrete requirement appears; such a change requires a new ADR.

## 10.5. Device configuration

Device selection (preferred GPU) and debug options (validation layers, GPU markers) are set through engine configuration variables. Gameplay code and ECS systems do not depend on these options.

## 10.6. Rendering pipeline

The planned rendering capabilities include:

- 3D mesh rendering.
- Camera support.
- Depth buffering.
- Materials.
- Texture sampling.
- Lighting.
- Physically based rendering.
- Normal mapping.
- Ambient occlusion.
- Clustered forward rendering.
- Frustum culling.
- Potential indoor visibility optimization.
- 2D GUI rendering.

The implementation should be incremental.

A simple forward renderer is the first milestone because it validates the complete graphics path with the least complexity.

It then evolves into a clustered forward renderer (ADR-0008) once the engine can load and render a useful scene and needs many lights.

## 10.7. Physically based rendering

PBR is a planned rendering feature.

The material system should eventually support physically based parameters such as:

- Base color.
- Metallic.
- Roughness.
- Normal map.
- Ambient occlusion map where applicable.
- Emissive properties where needed.

The implementation should follow a documented material workflow and consistent texture conventions.

Shader code should be organized to avoid unnecessary duplication between rendering passes.

Shaders are written in Slang and compiled offline to SPIR-V with `slangc` as part of the CMake build (ADR-0031). Shader hot reload is available in development builds.

## 10.8. Normal mapping

Normal mapping is a planned material capability.

The mesh format and import pipeline must provide the tangent-space data required by the selected normal-mapping approach.

The renderer must define how tangent, bitangent, and normal vectors are transformed.

Asset import and shader conventions must be consistent.

## 10.9. Ambient occlusion

Ambient occlusion is a planned rendering feature.

A screen-space technique may be appropriate for an initial implementation, but the exact technique remains an implementation decision.

AO must be treated as a rendering enhancement rather than a substitute for correct direct lighting or material behavior.

## 10.10. Clustered forward rendering

Superseded in Phase 9.12 by deferred lighting with ReSTIR DI (ADR-0044); kept for the record. Clustered forward rendering (Forward+) was the intended lighting architecture (ADR-0008). It supports many lights, as indoor scenes require, while keeping transparency, MSAA, and per-material shading models in a single pipeline, without the memory bandwidth and layout constraints of a G-buffer.

The planned pass sequence is:

1. Depth and normal prepass.
2. Light clustering (compute): lights are assigned to view-space clusters.
3. Ambient occlusion from the prepass depth and normals.
4. Opaque forward PBR pass, reading the cluster light lists.
5. Transparent forward pass, sorted back to front.
6. Post-processing if justified.
7. GUI rendering.

The exact pass sequence must be confirmed during implementation. A deferred G-buffer is reconsidered only if profiling shows a concrete need.

Do not build a complex render graph before the engine has a functioning renderer.

A render graph may be introduced later if the number of passes and resource dependencies justify it.

## 10.11. Indoor visibility optimization

Potential techniques include:

- View-frustum culling.
- Bounding-volume culling.
- Static geometry batching.
- Spatial partitioning.
- Portals connecting indoor regions.
- Hierarchical-Z GPU occlusion culling if measurements justify it.

The engine should first implement a correct, simple visibility pipeline.

Advanced indoor visibility techniques should be justified through representative test scenes and profiling.

## 10.12. Renderer extraction from ECS

The renderer should consume a well-defined representation of the visible scene.

ECS systems may identify renderable entities, but backend-specific command recording should not be embedded in generic ECS systems.

A conceptual flow is:

```text
ECS and scene state
    ↓
Render extraction
    ↓
Visible render items
    ↓
Culling and sorting
    ↓
Render passes
    ↓
RHI commands
    ↓
GPU execution
```

This separation allows gameplay logic and scene organization to evolve independently of rendering implementation.

---

## 10.13. Ray-traced rendering

From Phase 9 the renderer requires hardware ray tracing (ADR-0028). Rasterization draws a thin G-buffer, which compute passes light (ADR-0044); ray queries and a reference path tracer provide shadows, indirect light, reflections and volumetric shadows. Lighting is fully dynamic, with no baking. The denoiser, radiance cache and ReSTIR samplers are the engine's own, written from the published papers.

The plan in `docs/daidalos-engine-next-rendering-plan.md` describes the long-term target, including techniques deferred until measurements or content justify them. The owner's engine technical spec widens it to a horror game; ADR-0041 records what of it joins the roadmap.

---

# 11. Camera System

The engine must support cameras for rendering and gameplay.

Initial capabilities should include:

- Perspective projection.
- View transformation.
- Camera position and orientation.
- Near and far clipping planes.
- Field of view.
- Camera aspect ratio.
- Frustum extraction or equivalent visibility support.

Camera data should integrate with ECS without requiring camera logic to depend on a concrete graphics backend.

A camera may be attached to an entity and inherit its transform.

Future camera features may include:

- First-person camera.
- Third-person camera.
- Cinematic camera.
- Camera transitions.
- Camera constraints.
- Smooth interpolation.

A cinematic camera system is part of the planned engine functionality, but advanced camera sequencing should not delay the first working camera.

---

# 12. Input System

## 12.1. Objectives

The input system must provide a consistent abstraction for:

- Keyboard.
- Mouse.
- Gamepads.
- Button states.
- Axis values.
- Device connection and disconnection.
- Input events where appropriate.

## 12.2. Input abstraction

Gameplay code should not depend directly on operating-system input APIs.

The engine should define a stable interface that allows different platforms to provide equivalent input behavior.

An action-mapping layer may eventually map physical controls to semantic actions such as:

- MoveForward.
- MoveBackward.
- Jump.
- Interact.
- Pause.
- OpenInventory.

The action-mapping system should be introduced when a real gameplay requirement justifies it.

## 12.3. External library integration

SDL3 is selected for windowing and input integration (ADR-0004). SDL2 is not used, and SDL2 documentation or examples must not be applied to SDL3 without checking the SDL3 API.

The chosen library should be isolated behind engine interfaces so that gameplay code does not depend on SDL-specific types.

---

# 13. Physics System

## 13.1. Objectives

Physics should provide the functionality required by the intended games without creating an unnecessary maintenance burden.

Expected capabilities may include:

- Rigid bodies.
- Collision shapes.
- Collision detection.
- Collision response.
- Raycasts.
- Trigger volumes.
- Physics queries.
- Transform synchronization.
- Basic character movement support where required.

## 13.2. Third-party integration

A mature third-party physics library is preferred over implementing a complete physics engine from scratch.

Candidate libraries should be evaluated for:

- Platform support.
- License compatibility.
- API quality.
- Maintenance status.
- Performance.
- C++ integration.
- Build complexity.
- Suitability for the intended gameplay.

No particular physics library should be treated as finalized until the dependency is explicitly selected.

## 13.3. ECS integration

Physics bodies and collision data should be associated with entities through components or equivalent engine-level representations.

The physics integration must define:

- Which system owns the authoritative physical transform.
- How kinematic entities are updated.
- How dynamic body transforms are copied back into ECS.
- How scaling is handled.
- How entity destruction releases physics resources.
- How fixed-timestep simulation is integrated with the frame loop.

Avoid having physics and gameplay systems independently overwrite the same transform without a clear ownership policy.

Scale rule (see `docs/CONVENTIONS.md`): physics bodies accept only uniform scale. Non-uniform scale is baked into the collider shape, and a body placed under a parent with non-uniform scale triggers an assertion.

---

# 14. Audio System

## 14.1. Objectives

The audio system should support:

- Sound playback.
- Audio resource loading.
- Sound emitters.
- Listener positioning.
- Spatial audio.
- Volume control.
- Looping.
- Playback lifecycle.
- Basic audio categories or buses if needed.

## 14.2. Integration

Audio emitters should be attachable to entities.

The active listener should normally derive from the active camera or an explicitly selected listener entity.

Audio resources should be managed through the resource architecture without coupling generic resource loading to the renderer.

## 14.3. Third-party library

A mature audio library is preferred over implementing low-level audio device management and mixing from scratch.

The final library must be selected after evaluating licensing, platform support, features, and maintenance.

Advanced audio features should not delay basic sound playback and spatial positioning.

---

# 15. AI System

The AI system should provide reusable building blocks for gameplay behavior.

Potential initial features include:

- Finite-state machines.
- Behavior-state transitions.
- Basic decision-making.
- Perception queries.
- Navigation queries.
- Target selection.
- Pathfinding integration where required.

The initial architecture should avoid an overly generic AI framework.

AI logic should interact with entities through stable gameplay and ECS interfaces.

Navigation and pathfinding may require a dedicated third-party library depending on the types of environments and agents the engine needs to support.

A navigation mesh system should not be implemented until representative gameplay requirements are defined.

---

# 16. Gameplay and Cinematic Systems

## 16.1. Gameplay integration

The engine should provide the building blocks required to implement game-specific rules without embedding those rules in the engine core.

Examples include:

- Player controllers.
- Interaction logic.
- Health and damage.
- Triggers.
- Gameplay state.
- Object activation.
- Scene transitions.

These are examples, not mandatory engine-level components.

The engine should distinguish reusable infrastructure from game-specific logic.

## 16.2. Cinematic camera

The engine should support scripted or controlled camera behavior for in-game cinematics.

Potential features include:

- Camera movement along a path.
- Interpolation between camera states.
- Timed camera transitions.
- Look-at targets.
- Camera constraints.
- Coordination with gameplay events.

A simple cinematic controller should be implemented before a generalized cinematic sequencing system.

---

# 17. GUI and User Interface

## 17.1. In-game GUI

The engine must provide a 2D interface suitable for complete games.

Expected functionality includes:

- Main menus.
- Pause menus.
- HUD elements.
- Cursor rendering.
- Loading screens.
- Interactive buttons.
- Text display.
- Basic layout.
- Input interaction.
- UI state changes.

The UI architecture must support a clear relationship between input, widget state, layout, and rendering.

It should not require gameplay code to issue low-level graphics commands.

## 17.2. Developer GUI

An immediate-mode GUI library such as Dear ImGui is a candidate for development tools.

It may support:

- Debug windows.
- Entity inspection.
- Performance statistics.
- Rendering diagnostics.
- Resource inspection.
- Engine configuration.
- Development controls.

The developer GUI and in-game GUI serve different purposes and should not be conflated without a strong reason.

The editor may use Dear ImGui for its initial interface while the engine provides its own runtime GUI capabilities.

A Dear ImGui debug overlay is introduced with the first renderer (Phase 3), because it improves productivity during rendering work. It is compiled out of shipping builds (ADR-0015).

---

# 18. Scene Management

Scene management should organize world instances and support loading and unloading content.

Potential capabilities include:

- Scene creation.
- Entity instantiation.
- Entity hierarchy serialization.
- Resource references.
- Scene loading.
- Scene unloading.
- Scene transitions.
- Active camera selection.

The initial implementation may use a simple scene representation.

Serialization formats and sophisticated scene streaming should be chosen only after the basic entity and resource models are stable.

Scene loading must handle missing assets and invalid scene data without leaving the engine in an inconsistent state.

The scene system should not become a second resource manager or a replacement for ECS.

---

# 19. Asset Pipeline and Importing

The engine will eventually need a practical pipeline for preparing assets.

Potential inputs include:

- Meshes.
- Textures.
- Material descriptions.
- Shader source files.
- Audio files.
- Scene data.

The project should prefer standard asset formats and established import libraries when suitable.

Selected formats (ADR-0010): glTF 2.0 for meshes, materials, and scene fragments (loaded with fastgltf), and KTX2 for textures, stored in UASTC and transcoded at load to BC7 for color and BC5 for normal maps. PNG and JPEG remain accepted as a development fallback. Assimp is only considered if formats other than glTF become necessary.

The asset pipeline should define:

- Supported formats.
- Texture color-space conventions.
- Material parameter conventions.
- Mesh coordinate conventions.
- Normal and tangent generation.
- Asset path conventions.
- Missing-resource behavior.
- Import error reporting.

A custom binary asset format should not be created before there is a demonstrated need for faster loading, preprocessing, versioning, or packaging.

---

# 20. Development Tools and Editor

## 20.1. Objectives

The long-term project includes development tools that make engine usage and debugging easier.

Potential capabilities include:

- Entity hierarchy inspection.
- Entity and component editing.
- Transform inspection.
- Camera controls.
- Scene inspection.
- Resource inspection.
- Renderer diagnostics.
- Logging display.
- Performance counters.
- Debug rendering.

## 20.2. Incremental editor development

The editor must not become a prerequisite for engine development.

Initially, scenes may be created programmatically or through simple data files.

A minimal debug interface can be introduced when it materially improves productivity.

A more complete editor should follow after the engine has stable entity, scene, resource, and rendering interfaces.

## 20.3. Separation from runtime

Editor-specific functionality should not unnecessarily increase runtime dependencies.

The runtime engine should remain usable without launching the editor.

Where possible, editor operations should use public engine interfaces rather than directly manipulating private internal state.

---

# 21. Logging, Diagnostics, and Profiling

## 21.1. Logging

A library such as spdlog is a strong candidate for logging.

The engine should provide consistent logging conventions across modules.

Messages should identify relevant context, including:

- Module.
- Operation.
- Resource or entity where appropriate.
- Severity.
- Error details.

Logging should not become a substitute for error handling.

## 21.2. Assertions

Assertions should detect programming errors and violated invariants during development.

They should not be used as the only handling mechanism for recoverable runtime failures.

## 21.3. Profiling

The Tracy profiler is integrated from Phase 1 so that measurements exist before any optimization (ADR-0015). Crash minidumps on Windows and a small configuration-variable system are also introduced in Phase 1.

Potential measurements include:

- Frame time.
- CPU update time.
- Render submission time.
- Resource loading time.
- Physics update time.
- Entity and draw-call counts.
- GPU timing when supported.

Optimization must be based on measurements rather than assumptions.

---

# 22. Third-Party Libraries and Technology Choices

The engine should rely on mature libraries where they reduce implementation and maintenance costs.

The following table distinguishes candidate technologies from finalized decisions.

| Area | Candidate | Status and guidance |
|---|---|---|
| Build system | CMake + presets | Decided (ADR-0001) |
| Dependency management | vcpkg (manifest mode) | Decided (ADR-0001) |
| IDE | Visual Studio 2022 | Primary development environment |
| Graphics API | Vulkan 1.4 with hardware ray tracing | Decided, single backend (ADR-0007, ADR-0031) |
| GPU memory | Vulkan Memory Allocator | Decided (ADR-0007) |
| Shaders | Slang compiled with slangc to SPIR-V | Decided (ADR-0031, replacing HLSL and DXC of ADR-0009) |
| Upscaling | The engine's own temporal upscaling; AMD FSR 3.1 (MIT) as an option | Decided (ADR-0049, ADR-0050) |
| Denoising, radiance cache, many-light sampling | Own implementations | Decided (ADR-0028); NVIDIA RTX SDKs (NRD, SHaRC, RTXDI, DLSS) not used; their licence conflict ended with ADR-0047 |
| Math | GLM | Decided (ADR-0005) |
| Error handling | tl::expected | Decided (ADR-0003) |
| Logging | spdlog | Decided, behind an engine facade |
| Windowing and input | SDL3 | Decided (ADR-0004) |
| Developer GUI | Dear ImGui | Decided, from Phase 3 (ADR-0015) |
| Profiling | Tracy | Decided, from Phase 1 (ADR-0015) |
| Testing | GoogleTest + CTest | Decided (ADR-0002) |
| Model and scene format | glTF 2.0 via fastgltf | Decided (ADR-0010) |
| Textures | KTX2 (BC7/BC5); stb_image fallback | Decided (ADR-0010) |
| Serialization | None for now | glaze rejected until an editor needs it (ADR-0017) |
| Physics | Jolt Physics | Preferred; confirm by ADR in Phase 6 |
| Audio | miniaudio | Preferred; confirm by ADR in Phase 6 |
| Navigation | Recast/Detour | Only if a game requires it |

Before adopting any dependency, evaluate:

1. License compatibility.
2. Current maintenance status.
3. Windows and Linux support.
4. Compiler compatibility.
5. Build complexity.
6. API stability.
7. Runtime and binary footprint.
8. Suitability for the intended engine architecture.
9. Whether the dependency duplicates an existing capability.

Do not introduce a dependency solely because it is popular.

Record significant technology decisions and their rationale.

---

# 23. Testing Strategy

Testing is an essential part of the architecture, not an activity reserved for the end of development.

## 23.1. Unit tests

Unit tests should cover deterministic logic that can be validated independently.

Examples include:

- Math utilities.
- Transform composition.
- Hierarchy operations.
- Entity lifecycle.
- Component storage.
- Resource path normalization.
- Resource registry behavior.
- Configuration parsing.
- Gameplay-independent algorithms.

## 23.2. Integration tests

Integration tests should validate the interaction between modules.

Examples include:

- Application startup and shutdown.
- Platform and input integration.
- Resource loading.
- ECS and transform propagation.
- Scene loading.
- Renderer initialization.
- Shader and texture loading.
- Backend resource cleanup.

## 23.3. Graphics validation

Graphics functionality requires additional validation because successful compilation does not guarantee correct output.

Validation should include:

- A simple rendered scene.
- Camera movement.
- Mesh and texture rendering.
- Depth correctness.
- Material validation.
- Lighting validation.
- Resource destruction.
- Repeated startup and shutdown.
- Backend-specific validation where available.

Automated graphics tests render offscreen and compare against golden images with a tolerance. They run on software Vulkan implementations (lavapipe on Linux, SwiftShader on Windows) so that CI does not need a GPU, and the validation layers must report no errors.

From Phase 9, ray-traced passes are compared with the reference path tracer (FLIP). SwiftShader has no ray tracing; lavapipe has it from Mesa 24.1. Tests that need ray tracing are skipped, with a message, on a device without it, and always run on the developer's GPU.

## 23.4. Build validation

After each meaningful change, the relevant targets should be built and tests executed.

Do not claim that a change compiles or passes tests unless the corresponding commands have actually been run successfully.

When tests cannot be run, state the limitation explicitly.

---

# 24. Coding Standards

## 24.1. General style

Code should be readable, conventional, and consistent.

Follow the repository's existing naming and formatting conventions.

If conventions do not yet exist, establish a concise style guide and apply it consistently.

## 24.2. Interfaces

Public interfaces should expose only what consumers need.

Avoid leaking implementation details through headers.

Forward declarations should be used where they improve compilation boundaries without obscuring dependencies.

Headers must be self-contained and include the declarations required for their own public interfaces.

## 24.3. Templates

Template definitions should be placed where they are available to all required instantiations, typically in headers or included implementation files.

Use explicit instantiation where it offers a clear build or encapsulation benefit.

Do not move template definitions into a source file without accounting for which specializations are instantiated and where.

Static template registries must have well-defined linkage and ownership.

## 24.4. Memory management

Prefer deterministic lifetime and ownership.

Avoid unexplained raw owning pointers.

Avoid unnecessary global objects.

Avoid hidden initialization dependencies.

## 24.5. Error reporting

Errors must provide enough information to understand the failure.

Use a consistent strategy across modules.

Do not silently continue after a critical initialization failure.

## 24.6. Comments and documentation

Comments should explain intent, invariants, and non-obvious decisions.

Avoid comments that merely repeat the code.

Document public interfaces, ownership requirements, thread-safety assumptions, and important lifecycle rules.

---

# 25. Architectural Decisions and Anti-Patterns

Claude Code must actively avoid the following patterns.

## 25.1. God objects

Do not create a single Engine, Core, Manager, or Context class that owns every subsystem and exposes all functionality globally.

A small application-level composition object may coordinate initialization and lifetime, but individual systems should retain clear ownership and interfaces.

## 25.2. Universal service locator

Do not use a universal service locator as the default way to access engine functionality.

Explicit constructor dependencies and focused interfaces are preferred.

A limited context object may be appropriate for a clearly defined group of related services, but it must not become a hidden global dependency container.

## 25.3. Premature abstractions

Do not create interfaces for every class automatically.

Introduce abstractions when there is a real need for substitution, isolation, testing, or independent implementation.

The RHI is justified by isolating Vulkan from the renderer. It has no virtual interfaces, because a single backend gives them nothing to substitute (ADR-0007). A generic abstraction for every internal data structure is not automatically justified.

## 25.4. Premature optimization

Do not introduce custom allocators, lock-free data structures, job systems, or complex memory pools before a measured requirement exists.

## 25.5. Overgeneralized RHI

Do not attempt to abstract every Vulkan feature, or to design for hypothetical future backends, behind a universal API.

The RHI must expose the capabilities required by the engine while allowing backend-specific implementation details to remain internal where necessary.

## 25.6. Circular dependencies

Do not resolve circular dependencies by making every module depend on Core.

Redesign the interfaces and dependency direction instead.

## 25.7. Monolithic implementation changes

Avoid rewriting large parts of the engine in a single step.

Make focused changes that can be reviewed, built, and tested independently.

## 25.8. Unvalidated third-party assumptions

Do not invent library APIs or assume that a library supports a particular feature or platform.

Inspect installed versions, documentation, headers, and actual build behavior.

---

# 26. Development Roadmap

The roadmap below describes a logical sequence, not a fixed calendar.

Tasks should be adjusted according to actual repository state and technical discoveries.

## Phase 0 — Repository and Build Foundation

**Goal:** Establish a reliable development environment.

Deliverables:

- Repository structure.
- CMake project.
- Visual Studio 2022 integration.
- Compiler and language standard selection.
- Logging integration.
- Basic application executable.
- Unit test target.
- GitHub Actions CI (Windows MSVC and Linux Clang) and local build presets.
- Licence file and source headers (CC BY-NC-SA 4.0 at the time; MIT or Apache 2.0 since ADR-0047).
- Removal of the legacy Visual Studio projects and legacy Core code.
- Initial project documentation.

Completion criteria:

- The project configures and builds reliably.
- A minimal executable runs.
- Tests can be executed.
- Dependencies are documented.
- Debug and Release configurations are defined.

## Phase 1 — Platform and Application Lifecycle

**Goal:** Create a minimal cross-platform application foundation.

Deliverables:

- Platform interface.
- Window creation.
- Event processing.
- Application lifecycle.
- Frame timing.
- Input initialization.
- Clean shutdown.
- Error reporting for initialization failures.
- Tracy profiler, crash minidumps, configuration variables.

Completion criteria:

- The application creates a window.
- The window responds to close events.
- The main loop operates correctly.
- Shutdown releases owned resources.
- Platform-specific code is isolated.

## Phase 2 — Math and ECS Foundation

**Goal:** Build the engine's entity and transform model.

Deliverables:

- Math conventions documented.
- Entity identifiers.
- Entity creation and destruction.
- Component storage.
- Component addition and removal.
- System execution.
- Transform components.
- Parent-child hierarchy.
- Local and world transforms.
- Hierarchy tests.
- Keyboard, mouse and gamepad input state (moved from Phase 1's input initialization).

Completion criteria:

- Entities can be created and destroyed.
- Components can be added, accessed, and removed.
- Systems can process relevant entities.
- Parent-child transforms behave correctly.
- Invalid references and hierarchy cycles are handled.

## Phase 3 — First Rendering Backend

**Goal:** Render a basic 3D scene.

Deliverables:

- RHI.
- Vulkan 1.3 backend.
- Device initialization.
- Swapchain and presentation.
- GPU buffer creation.
- Shader compilation and loading.
- Vertex and index buffers.
- Camera.
- Depth buffer.
- Basic mesh rendering.
- Dear ImGui debug overlay.
- Shader hot reload.
- Clean renderer shutdown.

Completion criteria:

- A 3D mesh can be rendered.
- The camera can be moved.
- Depth testing works.
- Graphics initialization errors are reported.
- GPU resources are released safely.
- The application can repeatedly start and stop without obvious resource-lifetime failures.

The exact order of low-level RHI work should follow the actual requirements of the selected backend.

## Phase 4 — Resource and Material Foundation

**Goal:** Load real assets instead of relying exclusively on hard-coded geometry.

Deliverables:

- Mesh loading.
- Texture loading.
- Shader resources.
- Resource caching.
- Stable resource identity.
- Basic material representation.
- Resource lifetime rules.
- Test assets and sample scene.

Completion criteria:

- Assets can be loaded from files.
- Repeated requests avoid unnecessary duplicate loading.
- Missing or invalid assets produce useful errors.
- Resources can be released safely.
- Renderer and resource responsibilities remain separated.

## Phase 5 — Basic Scene and Gameplay Integration

**Goal:** Support a minimal playable or interactive scene.

Deliverables:

- Scene representation.
- Entity instantiation.
- Input integration.
- Basic movement.
- Camera control.
- Render extraction from ECS.
- Scene transitions.
- Input actions.
- Worker pool and background model loading.
- The factory level of the legacy demo, playable by flying (`docs/DEMO.md`).

Completion criteria:

- A scene can be initialized.
- Entities can move and render.
- Input is independent of operating-system APIs at gameplay level.
- Scene and resource lifetimes are coherent.

## Phase 6 — Physics, Audio and Animation

**Goal:** Integrate fundamental simulation and sound capabilities, and bring characters to life.

This phase comes before the modern rendering pipeline: a game that can be walked through and heard gives every later feature a real test, and the demo needs it most.

Deliverables:

- Selected physics library.
- Rigid-body and collider integration.
- Physics queries.
- Transform synchronization.
- Selected audio library.
- Audio resource loading.
- Sound emitters and listener.
- Spatial audio.
- Lifecycle and error handling.
- Skeletal animation: skinned meshes from glTF, with blending between clips.
- Character movement: walking, jumping, crouching and ladders.

Completion criteria:

- Physics state synchronizes with ECS correctly.
- Collision queries work in a test scene.
- Sound can be played from an entity.
- Audio follows the appropriate listener and emitter transforms.
- A skinned character plays its animations at the transform of its entity.
- Cleanup is reliable.

## Phase 7 — Modern Rendering Pipeline

**Goal:** Implement the intended material and lighting capabilities.

Deliverables:

- Material workflow.
- Physically based rendering.
- Normal mapping.
- Lighting system.
- Clustered forward rendering with depth and normal prepass.
- Light clustering.
- Ambient occlusion.
- Shadow maps for spot lights.
- Particles.
- Basic renderer diagnostics.
- Performance measurement.

Completion criteria:

- Multiple materials render correctly.
- Normal maps produce the expected lighting behavior.
- Lighting is consistent with the documented material workflow.
- Clustered light assignment matches a CPU reference on test scenes.
- AO can be enabled and disabled.
- Lights cast correct shadows, and the budget of shadowed lights is documented.
- Rendering resource lifetimes and synchronization are validated.

## Phase 8 — GUI and Developer Tools

**Goal:** Improve runtime usability and development efficiency.

Deliverables:

- Basic in-game UI rendering.
- Text and interactive controls.
- Menus and HUD support.
- Loading screen support.
- Developer debug UI extended (the overlay itself exists since Phase 3).
- Entity inspection.
- Rendering statistics.
- Resource diagnostics.

Completion criteria:

- Basic UI elements can be rendered.
- UI input is handled correctly.
- Developer tools do not require gameplay code to access backend internals.
- Runtime dependencies remain controlled.

## Phase 9 — Ray-Traced Rendering

**Goal:** Replace the rasterized lighting of Phase 7 with fully dynamic, ray-traced lighting (ADR-0028). The target is described in `docs/daidalos-engine-next-rendering-plan.md`; ADR-0028 records how the engine departs from it.

Hardware ray tracing is mandatory and no raster fallback is kept: each step deletes the technique it replaces, and the game stays playable after every step. Each step is its own pull request:

1. Vulkan 1.4, ray tracing (acceleration structures and ray queries) required at device creation, every shader moved to Slang (done, ADR-0031).
2. A minimal render graph: automatic barriers, debug labels, per-pass GPU timings (done, ADR-0032).
3. Acceleration structures: a BLAS per mesh, refit for skinned meshes, TLAS rebuilt each frame (done, ADR-0033; compaction deferred until memory asks for it).
4. A reference path tracer, a Cornell box scene and golden-image tests against it (done, ADR-0034; FLIP comes with step 5).
5. Ray-query shadows for every light, replacing the shadow atlas (done, ADR-0035: hard shadows; soft ones come with the denoiser).
6. A sky: cube textures, the level's sky seen through its openings and found by the rays that leave the scene (done, ADR-0036).
7. Motion vectors (skinned meshes and particles included), jittered projection and TAA, replacing MSAA and FXAA (done, ADR-0037).
8. An own spatiotemporal denoiser, a concrete class whose inputs and outputs are the seam for another; lights with a radius and soft shadows (done, ADR-0039).
9. Diffuse global illumination: an own hash-grid radiance cache and a ray-traced bounce, replacing SSAO and the ambient term; directional lights, for the moon (done, ADR-0040).
10. Ray-traced reflections below a roughness threshold, recursive on mirrors; reflection motion vectors for the denoiser; glass (thin for windows, thick with absorption for bottles); a mirror-room test scene. Replaces the ambient specular term (done, ADR-0042).
11. Physical units (ADR-0041): lights in lumens and candela with inverse-square falloff, color temperature in kelvin, emission in nits, exposure in EV. Replaces the legacy intensity and range (ADR-0022); the factory's lights are retuned (done, ADR-0043).
12. ReSTIR DI over every light and emissive triangle, replacing CPU light clustering, and deferred lighting: the pre-pass becomes a thin G-buffer, direct light moves to a compute pass and the forward mesh pass goes (ADR-0044). An emissive mesh is either a light or seen only by camera and reflection rays, never both. Test scene: a room of 1,000 lights and emissive panels (done, ADR-0044; 1.1 ms at 1080p in the factory, over its 1 ms budget).
13. Indirect light: the surface each pixel's ray hits is lit by the lights before the radiance cache is read; radiance clamped on indirect bounces only. ReSTIR GI was built and measured, and dropped: after the denoiser it lost to plain rays at equal cost. Test scene: a dark corridor lit only by the flashlight, which validates low-light quality before anything builds on it (done, ADR-0045; FLIP against the path tracer 0.243 to 0.112 there).
14. Image formation (ADR-0048): auto-exposure in EV within a range each level sets, so that the dark stays dark, adapting fast to light and slowly to the dark; bloom without threshold; AgX tone mapping; HDR10 output with the system's paper white and peak; a brightness calibration screen on first launch (done, ADR-0048).
15. Upscaling and dynamic resolution (ADR-0049): the engine's own temporal upscaling, grown from step 7's TAA, with its own sharpening, behind a seam where FSR 3.1 could go if it measures better against a supersampled reference; targets allocated at the output's size and drawn into a corner, so that a change of scale keeps every history; a frame-time target of 30 to 240 fps, down at once and up slowly (done, ADR-0049; the factory at 1440p goes from 11.5 ms to 6.6 ms at 67%).
16. FSR 3.1 from source (MIT), behind the upscaler's seam (ADR-0049): the engine's upscaler at 67% exceeded 1.15 times native resolution's FLIP in the mirror room; FSR is measured by the same test (done, ADR-0050: AMD's passes run through the engine's callbacks; 1 to 8% behind the engine's upscaler by FLIP in lit scenes, two to three times steadier; kept as `renderer.upscaler fsr`, the engine's the default).
17. Froxel volumetric fog with ray-traced shadows and local volumes (a dusty room, a steam pipe), replacing exponential fog (done, ADR-0051: the fog in the path tracer too; FLIP 0.090 against it on a lit Cornell box, 0.38 ms at 1440p in the factory; ReSTIR reuse per froxel measured worse and dropped; fog lit by many lights stays blotchy until lights are drawn by position; boxes of fog can lie on the ground, densest on the floor, placed by the level's glTF nodes).
18. Quality presets and no stutter (ADR-0041): Low, Medium, High and Ultra set the cost of every feature, chosen at first launch from the GPU; the pipeline cache persists on disk and is rebuilt after a driver or GPU change; a pipeline created during gameplay is logged as a bug; VRAM use against `VK_EXT_memory_budget` in the statistics; each feature has its debug view (done, ADR-0052: the presets are the eleven settings with a cost, measured in the factory, and a setting that is set wins over its preset; the first launch times High on the GPU and chooses, not a list of GPUs; Low to Ultra take 3, 4, 8.5 and 14 ms at 1080p on an RTX 3090; pipelines take 80 ms cold, 9 ms from the cache).
19. Particles as data, and how they look (ADR-0053): effects in JSON files, reloaded while the game runs; flipbooks with blended frames; color, opacity, size, emission and speed on curves over life; particles stretched along their velocity, turned around an axis, or lying in a plane; cone, cylinder, line and mesh spawn shapes, emission per meter, inherited velocity, bursts; a pool of 65,536, the sort narrowed to the alive particles if the whole pool costs over 0.3 ms. The factory's seven effects move to files unchanged (done, ADR-0053: the whole pool's simulation and sort take 0.14 ms at 1080p on an RTX 3090, against 0.11 ms for the old pool of 16,384, so the sort still covers the whole pool; spawning on a mesh's triangles moves to step 23, where burning objects and falling debris use it).
20. How particles move (ADR-0053): wind, from the level and from boxes of wind, curl-noise turbulence, attractors and vortices; collisions against the acceleration structure, replacing the screen-space ones, with bounce, rest or death; sub-emitters on death and on hits, spawned by the GPU; hits reported to the CPU two frames late, for decals and sounds (done, ADR-0053: drag pulls towards the air's velocity, so wind needs no setting of its own; only impacts report and spawn, slides and rests do not; 20,765 colliding, swirling particles simulate in 0.23 ms at 1080p on an RTX 3090).
21. Light and heat (ADR-0053): an emitter can own a light that follows its activity and flickers, an ordinary light for ReSTIR; lit smoke scatters with the fog's phase function, and six-way light maps shade flipbooks that have them; distortion particles bend the frame, for heat haze and shock waves. `texture-tool flipbook` makes the engine's own fire, smoke and explosion flipbooks, from a small simulation rendered by the path tracer's medium code (done, ADR-0053: the tool renders with its own CPU ray marcher, since the path tracer's media are boxes of fog; a light whose brightness changes counts as moved; the factory has fire, heat haze, smoke column and explosion effects, placed in 9.23).
22. Ribbons and mesh particles (ADR-0053): beams, arcs, lightning and tracers as strips the CPU builds; casings, shards, splinters, papers, debris and flies as meshes drawn into the G-buffer and lit like surfaces (done, ADR-0053: ribbon segments sort with the particles one by one, in the GPU's sort; ribbons glow and are lit by nothing; the third shape is a trail, where a moving emitter has been, not a fixed path; the simulation runs before the G-buffer; the sandbox's weapons throw casings, and `sandbox.showcase` puts the rest in front of the start).
23. The effects of an indoor horror game and weather seen from inside (ADR-0053): rain and snow zones in the level, around the camera and stopped by roofs; lightning through the windows; fire, candles, embers, smoke, heat haze, explosions, drips, a burst pipe, bubbles, dust motes, falling debris, ash, breath, blown papers, sparks, arcs, beams, impacts by surface, casings, tracers, blood, flies and moths, wisps, in a test room, each measured; the factory gets those it has a place for. Particles cost at most 1 ms at 1080p on an RTX 3090 in the factory, 1.5 ms in the test room under a storm; `renderer.particle_density` joins the presets (done, ADR-0053: split in two; this step built the systems, spawning on a mesh's triangles from the CPU, weather zones, lightning, emitters that no one may see emitting nothing, the density, what materials are made of, and the test room, a generated glTF level, `Factory/EffectsRoom.gltf`, with every effect of the list; 0.34 ms there under the storm).
24. The factory's effects (ADR-0053): fire, drips, sparks from a cable, dust in the lamps' cones, flies, rain in the big room in front of the start, and the weapons' impacts by the surface they hit, placed and measured against the factory's 1 ms.

Deferred until measurements justify them: meshlets, mesh shaders, the visibility buffer, Hi-Z occlusion, LODs, portals, async compute, resource aliasing, `VK_EXT_descriptor_heap`, opacity micromaps, shader execution reordering, adaptive ray budgets. Deferred until content needs them: ReSTIR PT, subsurface scattering. Out of scope: the neural rendering tier and NVIDIA's RTX SDKs (own implementations instead, see ADR-0028, ADR-0041 and ADR-0047).

Completion criteria:

- The factory runs at 60 fps at 1080p on an RTX 3090, on the High preset (met: 8 to 9 ms; Ultra 13 to 14 ms).
- No light leaks through the thin-wall test scene (met: `PathTracerTest`'s thin-wall scenes).
- Real-time output matches the path tracer within a FLIP threshold on the test scenes, the dark corridor included (met: the GPU tests' thresholds).
- Validation layers, synchronization validation included, report no errors (met: the GPU tests, and the factory at Ultra).
- Every pass reports its GPU time (met).
- No pipeline is created after a level has loaded (met: logged as an error if one is, and tested over every setting and debug view).
- Every effect of ADR-0053's list is a data file, placed by the level, within the particles' budget.

## Phase 10 — Level Editor

**Goal:** Build levels without code: import models, place lights, choose the sky, save (ADR-0041). Simple for now.

Deliverables:

- A level file: the models it places (glTF, with their transforms), its lights, its sky and atmosphere, the player's start and named entities for the game's code. The game loads it; a bare glTF still loads as today (done, 10.1, ADR-0054: `<name>.level.json`, typed components and `game` tags, the factory converted, its props in their own files).
- An editor executable, in development builds, whose viewport is the game's renderer, ray tracing included (done, 10.2, ADR-0055: `apps/editor`, the scene drawn into the dockspace's central node).
- Importing glTF models and sky textures into the assets (done, 10.4, ADR-0055: an asset browser with clay thumbnails drawn on workers; files dropped on the window are copied into the open folder; tiles placed by a double click or dragged into the view).
- Selecting by clicking in the viewport or in a hierarchy panel; move, rotate and scale gizmos; an inspector of the selected entity's components (transform, model, light) (done, 10.2: picking on the CPU, markers for entities without a mesh, every level component and the environment in the inspector).
- Undo and redo of every edit; saving and opening levels (done, 10.3, ADR-0055: the level before each edit, `LevelInstance::Sync` to follow; open, new, save as, autosave with recovery, a prompt before unsaved changes are lost).
- Snapping, a grid, multiple selection, duplicate, copy and paste between levels, camera bookmarks, finding entities and moving them under others in the hierarchy (done, 10.5, ADR-0055).
- Starting the game on the level being edited (done, 10.3: F5 from the start, Ctrl+F5 from the camera, unsaved changes included).
- The factory's lights that are added in code today move into its level file (done, 10.1: the moon, the alarms' beams, the dust and the steam, the exposure and the sky; the flashlight and the muzzle flash stay the player's).

Later phases add their own fields to the inspector (light profiles, visibility masks, porosity, triggers). Prefabs, painting tools, material, VFX and sequencer editors wait until content asks for them.

Completion criteria:

- A level built in the editor from imported models, lights and a sky plays in the game exactly as it looked in the editor.
- The factory loads from its level file and looks as before.
- Every edit can be undone.

## Phase 11 — Materials, Lights and Atmosphere

**Goal:** Give the engine what a dark, indoor horror game needs on screen (ADR-0041). Everything here works in the mesh pass, at ray hits and in the path tracer, through the one material function.

Deliverables, each its own pull request:

1. Layered materials on the OpenPBR model, read from glTF's `KHR_materials_*` extensions: clear coat, sheen, anisotropy, transmission (thin diffuse for lampshades and paper, frosted and tinted glass), thin film, emissive strength. Each lobe comes with a use in the factory or a test scene. (Done for clear coat and sheen, 11.1, ADR-0056; their textures, anisotropy and thin film wait for an asset that needs them.)
2. Lights: IES profiles, cookie textures, rectangle and disk area lights. One parameter drives a light and its emissive mesh together, with scriptable flicker, buzz, slow failure and power cuts. Candles, lighters, emergency lights and screens as presets.
3. The flashlight: an IES or cookie beam, sway tied to the camera's motion, a small offset from the eye so its shadows read, hooks for battery, flicker and failure.
4. Ray visibility masks: per entity, whether the camera, shadow, reflection and indirect rays see it, set in the editor and by the game's code. A figure seen only in a mirror, a shadow with no caster.
5. Decals evaluated in the material function, so they show in reflections and indirect light: blood, mold, grime, stains, and impacts (bullet holes, splatter, scorch marks).
6. Wetness: a porosity per material, wetness from vertex colors, textures and decals, darkening by porosity, a water coat (IOR 1.33), drying over time; puddles from a height and a water level, with ripples from drips, steps and impacts; running water on walls from flow maps. A flooded test room.
7. Particles in the acceleration structure (a BLAS rebuilt each frame, opt-out per effect), so smoke, sparks and mesh particles (step 9.22) cast shadows and show in reflections; smoke and steam as small density volumes lit by the volumetrics pass.
8. Camera effects: film grain, vignette, lens dirt lit by the flashlight, optional chromatic aberration and motion blur, depth of field from the camera's aperture.
9. Relief from height maps (ADR-0046): self-shadowing from the height map for direct light, so that the flashlight and lamps reveal mortar and cracks, with parallax where it reads; and occlusion baked from the same map by `texture-tool` into the red channel of the metallic-roughness texture, for indirect light. The factory's height maps wait in `assets/Factory/Source/`. Albedos that already paint their cavities dark are checked, so they are not darkened twice.

Deferred (ADR-0041): deep water with refraction and caustics, the GPU leak simulation, grid-based fire (flames are flipbooks plus a light, step 9.21), video textures, strand hair and skin rendering.

Completion criteria:

- Every new lobe and light type matches the path tracer within a FLIP threshold.
- The factory at 60 fps on the High preset with its new materials and effects.
- Each feature has a setting in the presets and a debug view.

## Phase 12 — Sound

**Goal:** Sound carries half the fear: make it come from the right place, through the right walls (ADR-0041).

Deliverables:

- Steam Audio (Apache 2.0) behind the `audio` module: HRTF, occlusion and transmission through walls and doors, reverb from traced paths, propagation around corners and through open doors, on worker threads over several frames.
- Acoustic properties on materials, next to the visual ones; footsteps and impacts per surface.
- Buses, snapshots and ducking on miniaudio's sound groups.
- Dynamic music in layers driven by a tension value, stingers for scares; sources that follow a light's flicker.

Completion criteria:

- A source behind a closed door sounds muffled, and comes from the door once it opens.
- The sound update stays within its CPU budget, measured.

## Phase 13 — Gameplay Systems

**Goal:** Give games the systems a horror game builds on: enemies that hunt, scripted scares, saves (ADR-0041).

Deliverables may include:

- Navigation meshes and pathfinding with Recast and Detour, with doors, vents and moving obstacles.
- Perception: how lit the player is (from the radiance cache) and how much noise they make (from the sound system).
- Behaviour trees or utility AI for stalking, searching, hiding and ambushing; AI state stays the game's.
- Events and sequences: timelines of light, sound, camera and entity cues, started by triggers. A scripting language (Lua) only when content is authored by someone who does not write C++.
- Cinematics: a sequencer for cutscenes and scares, camera paths, focus pulls and handheld shake.
- Save and load: checkpoints and saves of the world's state (doors, items, triggers, AI).
- Physics: picking up, dragging and throwing objects, leaning, ragdolls blended with animation, cloth through Jolt's soft bodies.
- Animation: layered state machines, additive layers, foot and hand IK, look-at, morph targets, the player's own body in first person.
- Portal visibility and scene-specific profiling, if measurements justify them.

Completion criteria:

- AI logic integrates cleanly with ECS.
- A scare can be authored without engine changes.
- A saved game restores the world the player left.
- Added complexity provides measurable or functional value.

## Phase 14 — Linux Validation

**Goal:** Validate the engine fully on Linux.

Linux builds and unit tests run in CI from Phase 0; this phase covers what CI cannot.

Deliverables:

- Full sample and golden-image suite running on Linux.
- Fixes for platform differences (file paths, case sensitivity, windowing and input behavior).
- Linux packaging of the samples.

Completion criteria:

- The same supported scenes run on Windows and Linux.
- Platform-specific differences are contained in the platform layer.
- No test is disabled on Linux without a documented reason.

## Phase 15 — Content Pipeline and Production Readiness

**Goal:** Make the engine practical for developing larger projects.

Potential deliverables:

- The level editor grown by need: prefabs, painting wetness and decals on surfaces, a material editor with live preview, trigger editing.
- Asset pipeline: mesh optimization (meshoptimizer), BC6H for HDR textures next to BC7 and BC5.
- Level and texture streaming, when a level no longer fits in memory or in one load.
- Accessibility: subtitles with speakers and sound captions, colorblind modes, toggles for motion blur, camera shake, film grain and chromatic aberration.
- Localization of text and subtitles.
- Input: full rebinding, gamepad rumble and trigger effects tied to tension.
- Diegetic UI: the game's interface drawn into textures on screens, notes and watches.
- Crash reports from the minidumps.
- Photo mode: a free camera, the path tracer converging, physical camera and image controls, high-resolution and EXR capture. The game provides its menu and rules.
- Packaging and deployment.
- Documentation and sample projects.

This phase is deliberately open-ended.

The editor must grow according to actual development needs rather than attempting to reproduce every feature of a commercial engine.

---

# 27. First Vertical Slice

The first end-to-end milestone should be deliberately small.

The goal is not to implement the whole engine. The goal is to validate the core architecture with the fewest moving parts.

Recommended vertical slice:

1. Build the application.
2. Create a window.
3. Initialize the first rendering backend.
4. Create a camera.
5. Render one simple 3D mesh.
6. Process keyboard and mouse input.
7. Move the camera.
8. Shut down cleanly.

This milestone validates the platform layer, application lifecycle, RHI, renderer, input, and build configuration.

The ECS can initially be minimal if necessary, but its intended integration boundaries should remain clear.

Once this vertical slice works, replace hard-coded mesh data with loaded assets, then introduce ECS-driven scene rendering.

---

# 28. Claude Code Operating Instructions

These instructions are intended to guide all AI-assisted development on Daidalos Engine Next.

## 28.1. Understand the repository before modifying it

Before making substantial changes:

1. Inspect the repository structure.
2. Read the existing implementation.
3. Identify the current architecture and dependencies.
4. Check the build configuration and compiler standard.
5. Identify existing conventions and incomplete features.
6. Compare the requested work with this knowledge base.
7. Determine the smallest safe implementation step.

Do not assume that the repository matches this document perfectly. The code may have evolved or some planned features may not exist yet.

When a discrepancy is found, report it and determine whether the document or implementation should change.

## 28.2. Do not invent existing functionality

Never claim that a module, class, test, build target, or feature already exists unless it has been verified in the repository.

Clearly distinguish between:

- Existing implementation.
- Planned implementation.
- Proposed improvement.
- Unresolved architectural decision.

## 28.3. Prefer small changes

For each task:

1. State the intended outcome.
2. Identify the affected modules.
3. Explain significant architectural decisions briefly.
4. Implement the smallest coherent change.
5. Build the affected targets.
6. Run relevant tests.
7. Review errors, warnings, and resource-lifetime implications.
8. Summarize the result and remaining limitations.

Do not rewrite unrelated modules merely to make the code stylistically uniform.

## 28.4. Ask before changing fundamental decisions

Do not silently change:

- Rendering API strategy.
- Module dependency direction.
- ECS architecture.
- Resource ownership model.
- Coordinate-system conventions.
- Build system.
- Core third-party dependencies.
- Public interfaces used by several modules.

If a fundamental decision must change, explain the problem, the alternatives, and the recommended solution before making a broad change.

Small local decisions may be made independently when they follow established project principles.

## 28.5. Verify library APIs

When using third-party libraries:

- Inspect the version actually installed or configured.
- Use the API supported by that version.
- Do not invent method names or configuration options.
- Confirm platform and compiler support.
- Keep dependency-specific details isolated.
- Document non-obvious integration choices.

## 28.6. Build and test honestly

Never claim that code compiles, tests pass, or a graphics feature works unless that result has been verified.

If the required toolchain, SDK, device, or runtime is unavailable, state what could not be tested.

A plausible implementation is not equivalent to a validated implementation.

## 28.7. Maintain architectural consistency

Before adding a class or subsystem, ask:

- What responsibility does it own?
- Which module should contain it?
- Which dependencies does it require?
- Does it duplicate an existing capability?
- Is the abstraction needed now?
- How will its lifetime be managed?
- How can it be tested?
- What is the simplest viable design?

## 28.8. Preserve developer understanding

When implementing complex C++ or graphics functionality, provide concise explanations of:

- Why the design was chosen.
- Important ownership rules.
- Initialization and shutdown requirements.
- Synchronization requirements.
- Important limitations.
- What should be tested next.

Avoid generating large volumes of unexplained code.

The goal is to help build a maintainable engine, not merely to produce code quickly.

## 28.9. Update documentation

When a meaningful architectural decision is finalized:

1. Update this knowledge base if the project-wide direction changes.
2. Add a short Architecture Decision Record when the decision has lasting consequences.
3. Update module documentation if its responsibilities or interfaces change.
4. Keep roadmap status aligned with actual implementation.

Do not update the document merely to describe every minor code change.

---

# 29. Definition of Done

A task is not complete merely because source code has been generated.

A task should be considered complete when the applicable criteria are satisfied:

- The implementation meets the stated requirement.
- The code follows existing project conventions.
- Module boundaries remain coherent.
- Ownership and lifetime behavior are defined.
- Relevant error cases are handled.
- The affected code compiles.
- Relevant automated tests pass.
- Manual validation is performed where automation is insufficient.
- No unrelated regressions are knowingly introduced.
- Documentation is updated when necessary.
- Unverified assumptions and remaining limitations are explicitly stated.

For graphics-related tasks, compilation alone is insufficient. Runtime behavior and GPU resource lifetime must also be considered.

For cross-platform changes, do not claim cross-platform correctness solely because the code compiles on Windows.

---

# 30. Project Risks

The principal project risks are:

## 30.1. Scope expansion

The combination of a custom renderer, two graphics APIs, ECS, physics, audio, AI, GUI, and editor tools can easily exceed the available development time.

**Mitigation:** Prioritize a working vertical slice and implement only capabilities required by the next milestone.

## 30.2. Graphics API complexity

Vulkan requires careful handling of synchronization, resource lifetime, and explicit GPU operations.

**Mitigation:** Use a single backend, Vulkan 1.3 features that simplify the API (dynamic rendering, synchronization2), validation layers in every Debug run, and a deferred-destruction queue from the first renderer.

## 30.3. Excessive abstraction

An attempt to support every future requirement can produce interfaces that are difficult to implement and debug.

**Mitigation:** Introduce abstractions based on concrete requirements, especially for backend independence and resource ownership.

## 30.4. AI-generated architectural drift

AI-assisted coding may introduce inconsistent naming, redundant abstractions, hidden dependencies, or code that compiles but does not follow the intended architecture.

**Mitigation:** Use this document as a reference, inspect diffs, validate builds, and require explicit justification for substantial architectural changes.

## 30.5. Ray tracing complexity

Ray-traced global illumination, denoising and resampling are research-grade techniques, and the engine implements its own.

**Mitigation:** A reference path tracer before any real-time technique, image comparison against it, one small step per pull request, GPU timings on every pass. Only one GPU (an RTX 3090) is available, so AMD and Intel behaviour stays unverified.

## 30.6. Integration risk

Subsystems may work independently but fail when combined because of incompatible coordinate conventions, ownership rules, initialization order, or resource assumptions.

**Mitigation:** Define conventions early, build integration tests, and validate vertical slices rather than developing every module in isolation for too long.

---

# 31. Immediate Next Steps

At the beginning of a development session, determine the current repository state before selecting the next task.

If the repository is still at the planning stage, the recommended next steps are:

1. Establish the repository structure.
2. Configure CMake and Visual Studio 2022.
3. Select the C++ standard.
4. Integrate basic logging.
5. Create a minimal executable and test target.
6. Establish formatting and coding conventions.
7. Implement the application lifecycle and window.
8. Validate input and shutdown behavior.
9. Begin the first rendering backend.
10. Maintain a small, prioritized backlog.

Do not start all engine modules simultaneously.

The initial objective is a minimal, reliable foundation that can be extended without architectural rework.

---

# 32. Final Project Philosophy

Daidalos Engine Next should be developed as a practical, maintainable engine rather than an attempt to reproduce the entire feature set of a commercial game engine.

The priorities are:

1. Correctness.
2. Architectural clarity.
3. A working end-to-end implementation.
4. Explicit ownership and lifecycle management.
5. Testability.
6. Maintainability.
7. Incremental feature development.
8. Performance improvements based on measurement.
9. Cross-platform support introduced deliberately.
10. Controlled scope.

When choosing between a sophisticated general-purpose solution and a simpler implementation that meets the current requirements, prefer the simpler implementation unless there is a concrete reason not to.

When choosing between implementing infrastructure from scratch and using a mature, suitable third-party library, prefer the library unless it conflicts with an important project requirement.

When choosing between adding another feature and stabilizing the current milestone, prioritize stabilization.

**The ultimate objective is to build a functional, extensible 3D engine that can support real game development while remaining feasible for a single developer.**
