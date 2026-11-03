# ADR-0016: Resources without a loader registry

- **Status:** Accepted
- **Date:** 2026-11-01

## Context

OVERVIEW §9 described a `ResourceManager` with loaders registered by type or file extension, and left the lifetime model open. The legacy code in `Core/` followed that pattern with `ILoader`, `IResource` and `CResourceManager`. The types of asset are known at compile time, and a registry only earns its cost when loaders are added from outside the engine, which is not planned.

Reference counting was also on the table. It adds cost to every copy of a handle, hides when things are released, and solves a problem that scenes do not have: their assets load together and leave together.

## Decision

- The `resources` module decodes files into plain CPU data (`ModelData`, `ImageData`) with typed functions: `LoadModelFile`, `LoadImageFile`, `DecodeImage`. It does not depend on the RHI. The dependency goes one way: `renderer` uses `resources`.
- There is no loader registry and no `ResourceManager`. `Cache<T>` stores values of one type by identity.
- The identity of an asset is its normalized path relative to an asset root: UTF-8, forward slashes, no `.` or `..`. The case is kept, and it must match the files on disk on every platform: a mismatch is an error, so that it shows up on Windows rather than breaking on Linux later.
- `renderer::AssetLibrary` (now `scene::AssetLibrary`, ADR-0021) turns CPU data into GPU objects and owns them. It releases everything together when it is destroyed. There is no reference counting and no eviction. A scene keeps a library for as long as it needs its assets.
- Stale handles are harmless: draws with a destroyed mesh are skipped, and draws with a destroyed material use the default one. Deferred destruction (ADR-0007) keeps frames in flight safe.
- A missing or invalid model or texture fails the whole load, with an error that names the file. Failed loads are not cached.
- Loading is synchronous. The CPU data has no GPU dependency, so the worker pool of ADR-0011 can decode it in Phase 5. Amended by ADR-0017, which adds `LoadModelAsync`.

## Consequences

- A texture shared by two models is loaded once, including by a model that spells its path differently.
- A file used in two ways, for example as color and as data, is two textures, because the use decides how it is decoded.
- Nothing can be unloaded individually. If that is ever needed, a smaller library per group of assets is the first answer.

## Alternatives considered

- A loader registry keyed by extension: indirection without a second implementation. To be reconsidered if audio in Phase 7 shows a real need.
- `std::shared_ptr` or counted handles: see the context above.
- Placeholder textures for missing files: friendlier during content work, but they hide broken assets. An editor could add them later.

## Amendment (2026-11-03): the library belongs to the scene

`AssetLibrary`, `Model` and `Instantiate` moved from `renderer` to `scene` (ADR-0021), because they create entities as well as GPU objects. A model stores each part once, and its nodes refer to parts by index, as glTF nodes refer to primitives.
