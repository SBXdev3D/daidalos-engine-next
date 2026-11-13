# ADR-0008: Clustered forward rendering

- **Status:** Superseded by ADR-0044 (2026-11-13)
- **Date:** 2026-10-27

## Context

OVERVIEW listed deferred rendering as the target architecture. The actual requirement is to support many lights in indoor scenes, together with PBR, normal mapping, and ambient occlusion.

## Decision

- Clustered forward rendering (Forward+).
- Passes: depth and normal prepass, light clustering in a compute pass, ambient occlusion from the prepass output, opaque forward PBR, sorted transparent forward pass, optional post-processing, GUI.
- The renderer starts as a simple forward renderer and becomes clustered once many lights are needed.

## Consequences

- Transparency, MSAA, and per-material shading models use one pipeline instead of a deferred path plus a separate forward path.
- No G-buffer: less memory bandwidth and no layout constraints on materials.
- Shading cost depends on the number of lights per cluster; light assignment is tested against a CPU reference.
- A G-buffer is reconsidered only if profiling shows a need (for example for screen-space reflections).

## Alternatives considered

- Deferred rendering: many lights, but transparency needs a second path, MSAA is expensive, and the G-buffer limits material variety.
- Plain forward rendering: simple, but cost grows with lights times objects.

## Amendment (2026-11-03): clusters assigned on the CPU

Light assignment runs on the CPU, not in a compute pass. The view is cut into 16×9 screen tiles and 24 depth slices, spaced exponentially from the near plane to 128 m. Each light's sphere is tested against the clusters of the slices it can reach, and the lists are packed by a counting sort and uploaded with the frame. The shader finds a pixel's cluster from its screen position and view depth, the same way as `ClusterOf` on the CPU.

The demo has about fourteen lights, so the CPU cost is negligible, and the RHI needs no compute pipelines for it. The tests are the CPU reference: on 20,000 random points of the view, every light that reaches a point is listed in its cluster. The shader's lookup does not change if assignment moves to the GPU later, when a scene needs many more lights.
