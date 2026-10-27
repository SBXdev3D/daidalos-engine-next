# ADR-0005: Math library and coordinate conventions

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

The renderer, physics, audio, and asset import must agree on handedness, axes, matrix layout, and depth range (OVERVIEW §8.6). Mismatches cause bugs that are hard to find.

## Decision

- GLM, with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and `GLM_FORCE_RADIANS` defined by CMake for every target.
- Right-handed, +Y up, −Z forward, +X right; meters, seconds, radians.
- Column vectors, column-major storage, `M = T * R * S`, `world = parentWorld * local`.
- Reverse-Z perspective projection with depth in [0, 1].
- Vulkan's downward clip-space Y is corrected once with a negative viewport height.
- The full table is in `docs/CONVENTIONS.md` section 5.

## Consequences

- These conventions match glTF 2.0, so imported assets need no axis conversion.
- GLM types may appear in public headers; they are the engine's math types.
- Each convention is covered by unit tests (axes, composition order, projection depth mapping).
- The legacy `CVector2`, `CRectangle` and `MathConstants` are deleted.

## Alternatives considered

- Left-handed (DirectX tradition): requires conversion of glTF assets.
- Z-up: common in modeling tools, but again differs from glTF.
- Custom math library: more code to write and test with no benefit.
