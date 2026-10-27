# ADR-0008: Clustered forward rendering

- **Status:** Accepted
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
