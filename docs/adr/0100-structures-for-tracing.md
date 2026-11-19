# ADR-0100: Structures built for tracing, and compacted

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 19.1 (ADR-0099). The frame's top-level structure was built with `PREFER_FAST_BUILD`, because it is rebuilt every frame (`rebuiltEveryFrame`). It holds a few hundred instances: its build takes about 0.3 ms with the skinned refits either way, and millions of rays cross it. The meshes' bottom-level structures were built for tracing, but kept the size the driver asks for before a build, which is an upper bound.

## Decision

- **The top-level structure is built for fast tracing.** `rebuiltEveryFrame` stays for structures whose build is the larger cost, such as the particles' boxes.
- **Bottom-level structures built with the uploads are compacted** (`Device::BuildWithUploads`). The build allows compaction and writes its compacted size to a query, recorded with the uploads. At a later `BeginFrame`, once those uploads are done, the device creates a structure of that size, records a compacting copy with the next uploads, and puts the new structure, its storage and address in place of the old one, which is destroyed after the copy. A barrier after the copies makes them visible to everything submitted later.
- **The address of such a structure changes once:** instances are written after `BeginFrame`, each frame, as the renderer already does (`RayTracingScene::Prepare`). The device header says so.
- At most 4096 builds wait for their size at a time; more are left as built.
- The developer tools' Device panel shows the structures' count and memory. Each batch logs its sizes at debug level.

## Measurements

At 1080p on an RTX 3090, dressed factory.

**Memory:** the factory's 150 bottom-level structures went from 30.7 MB to 13.6 MB, 44% of their size as built.

**Time**, old and new build in turns, two runs each:

| | Acceleration structures | Direct light | Indirect light | Reflections | Frame |
|---|---|---|---|---|---|
| High, loading floor | 0.33 → 0.33 to 0.34 ms | 2.71 to 2.75 → 2.68 ms | 3.10 to 3.21 → 3.04 to 3.07 ms | 1.67 → 1.62 ms | 12.00 to 12.12 → 11.82 to 11.83 ms |
| Medium, loading floor | 0.31 to 0.33 → 0.33 ms | 1.07 → 1.03 to 1.06 ms | 0.78 to 0.79 → 0.80 ms | 0.78 to 0.81 → 0.78 to 0.82 ms | 5.64 to 5.73 → 5.69 ms |

On High, every ray pass is a little faster, about 0.2 ms in all; on Medium, whose rays are fewer, the change is within the runs' spread. The top-level build costs 0.01 to 0.04 ms more. The start room switches between two states about 1.5 ms apart with either build, as noted in ADR-0097, and is not compared.

The top-level flag alone, measured first in three runs each way: direct light on the loading floor 0.05 to 0.1 ms faster in five of six pairs.

**The image** does not change: the same triangles are found; only the structure around them is laid out differently.

## Consequences

- The tests check that a compacted structure is smaller and that rays still hit its instances where they were.
- A level that streams meshes in compacts them a frame or two after they arrive.

## Alternatives considered

- **Compacting at the end of loading, waiting for the GPU:** simpler, but a mesh created later, by an import in the editor or a prefab, would not be compacted.
- **`PREFER_FAST_TRACE` with `ALLOW_UPDATE` for the top level, refitting instead of rebuilding:** instance order changes from frame to frame (ADR-0089's culling, draws added and removed), which a refit cannot follow.
