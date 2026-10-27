# ADR-0010: Asset formats

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW §19 asks for standard formats and no custom binary format until there is a need. Loading PNG or JPEG at runtime gives no mipmaps and no GPU compression, which costs memory and load time and hurts normal-map quality.

## Decision

- Meshes, materials, and scene fragments: glTF 2.0, loaded with fastgltf.
- Textures: KTX2, with BC7 for color textures and BC5 for normal maps, produced by an offline step (`toktx` or `texconv`) driven by CMake.
- PNG and JPEG (stb_image) are accepted as a development fallback.
- Scene files: JSON (glaze preferred, confirmed in Phase 5).

## Consequences

- No custom binary format.
- glTF conventions match the engine conventions (ADR-0005), so no axis conversion is needed.
- Assimp is only considered if formats other than glTF become necessary.

## Alternatives considered

- Assimp for everything: a large dependency supporting many formats the engine does not need.
- Custom binary formats: faster loads, but premature.
