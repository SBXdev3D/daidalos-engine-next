# Daidalos Engine Next — Conventions

This file holds the rules that every module must follow. Project-wide direction lives in `OVERVIEW.md`; the reasons behind decisions live in `docs/adr/`.

Each rule that can be checked by a test should have one (see "Tested by" notes).

---

## 1. Code style

> **Status: accepted (2026-10-27).** The legacy code used `C`/`S`/`E` type prefixes and `UPPER_CASE` enum values. This convention drops the type prefixes and keeps the parts of the old style that are still common (PascalCase methods, `m_` members, `I` for pure interfaces).

| Element | Convention | Example |
|---|---|---|
| Namespaces | `daidalos::<module>`, lowercase | `daidalos::ecs` |
| Types (class, struct, enum) | PascalCase, no prefix | `Window`, `FrameTimer`, `KeyCode` |
| Pure interfaces | `I` prefix | `IResourceLoader` |
| Functions and methods | PascalCase | `CreateEntity()` |
| Local variables, parameters | camelCase | `deltaTime` |
| Member variables | `m_` + camelCase | `m_entities` |
| Static members | `s_` + camelCase | `s_instanceCount` |
| Constants and `constexpr` | `k` + PascalCase | `kMaxGamepads` |
| `enum class` values | PascalCase | `KeyCode::Escape` |
| Macros | `DAI_` + UPPER_CASE | `DAI_ASSERT` |
| Files | PascalCase, matching the main type | `FrameTimer.hpp`, `FrameTimer.cpp` |
| Header extension | `.hpp`; source `.cpp` | |
| Header guards | `#pragma once` | |

Formatting is defined by `.clang-format` (clang-format 19) and is not discussed in reviews: run `python scripts/format.py` before committing; CI runs it with `--check`. Static analysis is defined by `.clang-tidy`, whose naming rules mirror the table above.

### Source file header

Every source file starts with:

```cpp
// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0
```

## 2. Module layout

```text
engine/<module>/
  include/daidalos/<module>/   public headers, included as <daidalos/<module>/X.hpp>
  src/                         private headers and sources
tests/unit/<module>/           unit tests for that module
```

- Each module is one static library target, `daidalos::<module>`.
- Dependencies are declared only with `target_link_libraries`. A module must not include another module's headers without linking it.
- Third-party types (SDL, Vulkan, spdlog, Jolt, miniaudio) must not appear in public headers, except GLM math types (ADR-0005) and `tl::expected` through `Result` (ADR-0003). The profiling macros in `daidalos/core/Profile.hpp` expand to Tracy calls in profile builds; no Tracy type is named in engine headers.
- GLM is used through `daidalos::math`, which sets its configuration macros for every target that links it. Code does not link `glm::glm` directly.
- Tests of a module's private code (for example `platform`'s SDL event translation) add the module's `src/` directory to the test target's include path. Only tests may do this.

## 3. Ownership and lifetime

- Owning pointers are `std::unique_ptr`. `std::shared_ptr` requires a comment explaining who shares ownership.
- Raw pointers and references are non-owning. A raw pointer means "may be null"; a reference means "never null".
- References to entities and resources that can disappear use generational handles (`Entity`, `Handle<T>`), never pointers.
- No global mutable objects, singletons, or service locators. Dependencies are passed through constructors.
  The only exceptions are process-wide by nature: logging and the assert handler (every module must be able to report problems without a logger being passed everywhere), and the crash handler. They are still configured explicitly at startup (`logging::Initialize`, `core::SetAssertHandler`, `platform::InstallCrashHandler`).
  SDL also has process-wide state, so only one `platform::Platform` can exist at a time; `Platform::Create` enforces it.
- Every class that owns a resource is non-copyable unless copying is meaningful; moves are explicit.

## 4. Errors, assertions, logging

- A function that can fail returns `Result<T>` (`tl::expected<T, Error>`). `Error` holds a code, the module name, and a message with context (path, entity, operation).
- `DAI_ASSERT(condition, message)` checks programming errors and invariants. It is never used for failures caused by data, files, or devices.
- No exception crosses a module boundary. Code that calls a throwing API catches at the call site and returns an `Error`.
- Logging goes through the engine facade: `DAI_LOG_INFO("module", "message {}", value)`. Logging does not replace returning an error.
- Startup steps that can fail are added to an `app::StartupSequence`, which stops the steps already started when a later one fails.

## 4.1. Configuration variables

- Settings that a user or tester may change without recompiling are cvars, registered in the application's `core::CVarRegistry` by the module that owns them: `Register<std::int64_t>("window.width", 1280, "description")`.
- Names are lowercase, dot-separated by module: `window.width`, `renderer.vsync`, `physics.substeps`.
- Values come from the config file (`name value` lines) and the command line (`+name value`); the command line wins. A value that does not parse fails the startup if the variable is already registered, and is logged as a warning otherwise. Unknown names are logged as warnings.

## 5. Math and coordinate conventions

Decided in ADR-0005. Tested by: `tests/unit/math`.

| Topic | Convention |
|---|---|
| Library | GLM, with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and `GLM_FORCE_RADIANS` defined globally by CMake |
| Handedness | Right-handed |
| Axes | +X right, +Y up, −Z forward (camera looks down −Z) |
| Units | Meters, seconds, radians |
| Vectors | Column vectors; a point is transformed as `M * v` |
| Matrix storage | Column-major (GLM default), uploaded to shaders as-is |
| Local transform | `M = T * R * S` (scale, then rotate, then translate) |
| Hierarchy | `world = parentWorld * local` |
| Quaternions | `glm::quat`, unit length, `(w, x, y, z)`; positive angles rotate counter-clockwise looking down the axis |
| Euler angles | Only at the edges (editor, config). Order: yaw (Y), then pitch (X), then roll (Z) |
| Projection | Perspective, right-handed, depth range [0, 1], **reverse-Z** (near plane → 1, far plane → 0), infinite far plane allowed |
| Depth test | `GREATER_OR_EQUAL`, depth cleared to 0 |
| Clip-space Y | Vulkan's clip-space Y points down. This is corrected once, with a negative viewport height. Matrices and shaders never flip Y |
| Front faces | Counter-clockwise (glTF convention) |

These match glTF 2.0, so imported assets need no axis conversion.

## 5.1. ECS

Decided in ADR-0006. Tested by: `tests/unit/ecs`.

- Components are plain data stored by value in `ecs::World`. Engine components only (ADR-0006); game components live in game code.
- A reference or pointer to a component is valid until a component of the same type is added or removed. Keep `Entity` handles, not component pointers, across frames.
- Inside `World::Each<Ts...>`, components of the types `Ts` cannot be added or removed, and entities that have them cannot be destroyed (`DAI_ASSERT`). Collect the entities and change them after the loop.
- Systems run from an `ecs::Schedule`, in the order they were added. The order is written where the schedule is built.
- Parent links are changed only with `World::SetParent`. Destroying an entity destroys its descendants.
- Edit `LocalTransform`, never `WorldTransform`. `PropagateTransforms` recomputes every world transform; run it once per frame after the systems that move entities and before anything reads `WorldTransform` (rendering, audio, physics synchronization).

## 5.2. Input

- Keys are physical positions named after a US QWERTY keyboard (`Key::W` is the key above `S` on every layout). Text entry will use text events, not keys.
- Mouse positions are window coordinates from the top-left corner, +Y down. Gamepad sticks are in [-1, 1] with +Y up; triggers in [0, 1]. No dead zone is applied: game code applies its own.
- `WasPressed` and `WasReleased` last one frame: read them in `OnUpdate`, not `OnFixedUpdate`, which may run zero or several times per frame.

## 6. Rendering conventions

| Topic | Convention |
|---|---|
| Color textures (base color, emissive) | sRGB formats |
| Data textures (normal, metallic-roughness, occlusion) | Linear (UNORM) formats |
| Metallic-roughness texture | glTF layout: roughness in G, metallic in B, occlusion optionally in R |
| Normal maps | Tangent space, +Y up ("OpenGL" convention, as in glTF), stored as BC5 (X, Y), Z reconstructed |
| Tangents | `vec4`: xyz = tangent, w = ±1 handedness; `bitangent = cross(normal, tangent.xyz) * tangent.w`. Generated with MikkTSpace when missing |
| Lighting | Computed in linear space; output converted to sRGB by the swapchain format |
| Light units | Point and spot lights in candela, directional lights in lux (glTF `KHR_lights_punctual`) |
| GPU resource destruction | Only through the deferred-destruction queue, never immediately while frames are in flight |

## 7. Physics conventions

- Physics uses the same axes and units as section 5.
- Ownership of the transform: dynamic bodies are written by physics into ECS; kinematic bodies are written by ECS into physics; static bodies are set once on creation.
- **Physics bodies accept only uniform scale.** Non-uniform scale is baked into the collider shape when it is created. A body whose parent chain has non-uniform scale triggers `DAI_ASSERT`.
- The simulation runs at a fixed 60 Hz step; rendering interpolates with the accumulator's remaining fraction.

## 8. Asset paths

- Asset paths are relative to the asset root, use `/` as separator, and are normalized (`.` and `..` resolved) before being used as identities.
- Matching is case-sensitive everywhere. On Windows, a path whose case differs from the file on disk is loaded but logs a warning, so the error is found before running on Linux.

## 9. Tests

- Framework: GoogleTest, run through CTest (ADR-0002).
- Test names: `TEST(ModuleOrClass, BehaviorUnderCondition)`, for example `TEST(Registry, DestroyedEntityHandleIsInvalidAfterReuse)`.
- Unit tests never use a real window, GPU, or audio device. Use the SDL dummy video driver (`PlatformConfig::videoDriver = "dummy"`), the miniaudio null backend, a mock RHI, or software Vulkan (lavapipe, SwiftShader).
- Behavior that ends the process (crashes) is tested by running a helper executable and checking what it leaves behind, not with death tests: GoogleTest catches structured exceptions on Windows before a crash handler sees them.
- Failed assertions are tested with an assert handler that throws (`DAI_EXPECT_ASSERT` in `tests/unit/ecs/AssertCapture.hpp`), so the code under test stops before it touches invalid state. These tests are skipped when assertions are compiled out.
- Every bug fix comes with a test that fails before the fix.

## 10. Git

- `main` is always buildable and its tests pass.
- One logical change per commit, with a message in the imperative mood ("Add frame timer").
- Third-party code is never committed; it comes from vcpkg.
