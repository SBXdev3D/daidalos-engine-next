# ADR-0017: Scenes without a file format

- **Status:** Accepted
- **Date:** 2026-11-01

## Context

Phase 5 needs scenes that own a world and its assets, and that can be replaced while the game runs. OVERVIEW §22 also expected a serialization library (glaze) and a scene file format in this phase. Nothing would read or write such files yet: there is no editor, and a level's geometry and hierarchy already fit in glTF.

The sandbox also showed that lifetimes were wrong. It owned its `AssetLibrary` directly, and `OnStop` is not called after a failed `OnStart`, so a level that failed to load destroyed its library after the renderer, and crashed.

## Decision

- `scene::Scene` owns a `World`, an `AssetLibrary`, two `Schedule`s (fixed steps and frames), the active camera and the atmosphere. The assets of a scene die with its entities.
- `Application` holds the current scene. `SetScene` takes effect between two frames. The application's startup sequence has a step that releases the scene, so it always goes before the renderer, whichever way the application ends.
- The application runs the scene's systems, computes its transforms and extracts it for rendering, so a game no longer does that itself.
- A level is a glTF file plus code. Model nodes reach the ECS through `renderer::NodeName` (now `scene::NodeName`, ADR-0021), and the game finds what it needs by name. glaze is not adopted, and there is no scene file format.
- `core::WorkerPool` is the pool of ADR-0011. `AssetLibrary::LoadModelAsync` reads and decodes on a worker, and creates GPU objects on the main thread. A library that dies cancels its loads.
- To replace a level, the next scene loads in the background and takes over once it is ready, so a level is never shown half-built.

## Consequences

- A loading level does not freeze the window, except for the moment when its GPU objects are created, which still happens in one go on the main thread.
- A texture shared by two models is decoded twice and created once, because workers cannot look into the cache.
- Levels cannot carry gameplay data beyond node names. The sandbox encodes attributes in those names, like `door;id=-5;radius=-1`, which is enough for one level and awkward for many. An editor (Phase 11), or glTF `extras`, is the way out.
- Entities cannot be saved. Nothing needs that yet.
- A system added to a scene must not capture anything that dies before the scene.

## Alternatives considered

- A JSON scene format with glaze: a format, a dependency and a loader with no user. To be reconsidered with the editor.
- A stack of scenes for menus over a game: no need yet.
- Keeping the scene in the handler and fixing the sandbox: the same mistake would come back with the next game.
