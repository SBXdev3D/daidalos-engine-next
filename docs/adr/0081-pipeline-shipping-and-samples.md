# ADR-0081: The asset pipeline as measured, a checked archive, a guide and a sample

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 still lists, after the editor's growth and the crash reports (ADR-0078 to ADR-0080):

- the asset pipeline: mesh optimization with meshoptimizer, and BC6H for HDR textures next to BC7 and BC5;
- level and texture streaming, when a level no longer fits;
- packaging and deployment;
- documentation and sample projects.

The rule of this phase (ADR-0041) is that each part comes when content needs it.

## Decision

### The asset pipeline: measured, and left as it is

- **Mesh optimization waits.** Every model under `assets/` together is 23,122 triangles and 51,958 vertices. In the factory at 1080p on an RTX 3090, the G-buffer pass takes 0.55 ms of an 11.6 ms frame (`renderer.log_gpu_times`), and most of that is shading pixels, not transforming vertices. Reordering indices for the vertex cache, or vertices for fetching, has well under a tenth of a millisecond to gain. Ray tracing gains nothing either: its cost is per ray, not per vertex.
  - meshoptimizer comes when a level has a model of more than about 100,000 triangles, or the G-buffer pass passes 1.5 ms. Both are likely in Phase 16, if dense CC0 models arrive.
  - It would come with LODs from `meshopt_simplify` if distance makes them pay. Vertex cache and fetch ordering at import would come too, being nearly free.
- **BC6H waits:** the engine has no HDR texture. The skies are six PNG faces scaled by an intensity. An HDR sky or light probe would bring BC6H, through KTX2's UASTC HDR mode if libktx supports it then, and otherwise through a BC6H encoder in `texture-tool`.
- **Streaming waits** (ADR-0041): the factory loads in one go and uses about 1 GB of video memory out of 24.

### Packaging: the archive is checked as a player gets it

- The archive itself is unchanged (ADR-0038): the game, its settings, `assets/`, the licences and the third-party notices.
- `scripts/check_package.py` unpacks it into an empty folder, with no build tree to fall back on. It checks that what a player needs is there, and that development files are not: the editor, the tools, the sample, PDBs and import libraries. It then starts the game from that folder and requires it to:
  - still be running after 25 seconds;
  - leave no crash dump;
  - write no error to its log (warnings are listed).
- The tools and the sample are built in every configuration, the shipping one included, but not installed.

### Documentation and a sample

- **`docs/GUIDE.md`** is how a game is made on the engine, where `ARCHITECTURE.md` is how the engine is made:
  - a program and its settings;
  - a scene and a level;
  - building levels in the editor;
  - gameplay through tags, events, physics, navigation, sound, input, saves, text and the interface;
  - the content pipeline;
  - shipping.

  Each part points to its code and its ADR.
- **`samples/level-viewer`** is the smallest program that shows a level: an application, a scene, a level made into entities, and a camera that flies with the mouse and keys. It is about 140 lines. `DAI_BUILD_SAMPLES`, on by default, builds it with the engine.

## Consequences

- Phase 15 is done, except:
  - the diegetic interface and streaming, deferred with their reasons (ADR-0041);
  - photos larger than the window (ADR-0077);
  - gamepad trigger effects (ADR-0076);
  - mesh optimization and BC6H, now with the measurements that bring them.
- The sample is one more program that breaks when an engine interface changes. That is its point: it is the first user of the engine besides the demo and the editor.
- A release is checked by a script before it is tagged, not only by CI building it.

## Alternatives considered

- **meshoptimizer now, at import:** nearly free, and one more vcpkg dependency for a gain the GPU timings cannot show. Phase 16's content decides.
- **A sample per subsystem** (physics, sound, particles): more code to keep working. The sandbox already uses every subsystem, and the guide points into it.
- **An installer** (MSI, AppImage): the archive runs where it is unpacked, and nothing about the game needs installing.
