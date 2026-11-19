# ADR-0098: GPU-driven rendering measured, not needed; the presets measured again

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.6, the last of Phase 18 (ADR-0092): draws compacted on the GPU, with two-phase occlusion against the previous frame's depth (Hi-Z). Occlusion culling saves the work of what is drawn and then hidden: its vertices, and its pixels until the depth test rejects them. Before building it, how much that work costs in the factory.

- **The G-buffer takes 0.7 to 0.9 ms** of a 12 ms frame on High, at 1080p on an RTX 3090 (0.4 to 0.5 ms on Medium). It is the only pass that occlusion culling makes cheaper: shadows, reflections and indirect light trace against the frame's whole acceleration structure.
- **Triangles are cheap:** ten times as many cost 0.05 ms (ADR-0090). Portals (ADR-0089) already leave out the rooms the camera cannot see.
- **Hidden pixels:** the G-buffer's material is heavy (decals, wetness, relief in eight directions), and draws are sorted by pipeline and mesh, not by distance, so a surface drawn before the wall in front of it is shaded in full. If that cost much, drawing nearest first would save it. Drawing the G-buffer nearest first, measured old against new, two runs each:

| | Start room | Loading floor |
|---|---|---|
| Medium | 0.49 to 0.51 → 0.55 to 0.58 ms | 0.38 to 0.40 → 0.41 to 0.44 ms |
| High | 0.86 to 0.87 → 0.93 to 0.94 ms | 0.71 → 0.73 to 0.77 ms |

  Slower: the meshes change from one draw to the next instead of once per mesh, and that costs more than the hidden pixels.

So the hidden surfaces cost about 0.1 ms at most, and a Hi-Z pyramid, a culling dispatch and the second phase for what the first missed cost about as much.

## Decision

- **No GPU-driven rendering for now.** It is recorded as measured and left out, as levels of detail were (ADR-0090). The G-buffer keeps its order. It becomes worth building with a level of many more draws (thousands, where the CPU's recording shows in Tracy) or with large open views that portals cannot cut.
- **Async compute stays off** (ADR-0095): it was to be measured again with a depth pass for Hi-Z, which there is not.
- **The presets' costs measured again** (ADR-0052, ADR-0092), two runs, dressed factory, at 1080p on an RTX 3090:

| Preset | Start room | Loading floor | Share of High |
|---|---|---|---|
| Low | 3.21 to 3.26 ms | 3.73 to 3.75 ms | 0.29 |
| Medium | 5.00 to 5.01 ms | 5.67 to 5.70 ms | 0.44 |
| High | 12.31 to 12.61 ms | 11.89 to 11.97 ms | 1.0 |
| Ultra | 12.96 to 13.08 ms | 13.83 to 14.35 ms | 1.11 |

  Low and Medium keep 0.28 and 0.43, within the runs' spread. Ultra goes from 1.2 to 1.1: since the denoiser's change (ADR-0097), which saved more on Ultra, it costs 10% more than High instead of 20%. With `renderer.quality auto`, a GPU whose High frame takes 11.1 to 12.1 ms gets Ultra at 60 fps instead of High.

## Consequences

- Phase 18 is done. Before it (ADR-0093), the frame took Low 3.8 / 4.8, Medium 6.5 / 7.7, High 13.1 / 15.6 ms in the start room / on the loading floor; after, Low 3.2 / 3.7, Medium 5.0 / 5.7, High 12.5 / 11.9 ms. The loading floor also lost its overhead crane in the editor meanwhile.
- The rest of the High frame is ray tracing: direct light, indirect light, reflections and the denoiser make up about 8 ms of it. The next gains are there, not in rasterization.

## Alternatives considered

- **A depth pre-pass,** then the G-buffer testing for equal depth, so no hidden pixel is shaded: it draws every triangle twice to save what drawing nearest first showed to be small.
- **Building Hi-Z culling to measure it:** the measurements above bound its gain; the machinery (indirect draws per mesh, a pyramid, two phases, the instance order the ray tracing relies on) would be kept up for nothing.
