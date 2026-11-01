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

## Amendment (2026-11-01): UASTC, transcoded at load

**Neither tool named above can produce this.** `ktx create` and `toktx` encode Basis Universal (UASTC, ETC1S) and ASTC, not BC7, and `texconv` writes DDS, not KTX2. Textures are therefore stored in KTX2 as **UASTC** and transcoded to BC7 (BC5 for normal maps) when they are loaded, with libktx. glTF refers to them through the `KHR_texture_basisu` extension. The offline tool is `ktx create --encode uastc`, from the `ktx` port with its `tools` feature.

The engine reads such files, and the tests encode one with libktx. No asset pipeline step produces them yet: the sample scene uses the PNG files that it ships with, and the CMake-driven step of the decision above remains to be written when a project needs it.

**PNG and JPEG get a full mip chain** computed on the CPU when they are loaded, in linear space for sRGB colors.

**Tangents and normals.** Vertices have room for tangents and keep those of the asset, but missing tangents are only generated with normal mapping in Phase 6 (the glTF specification asks for MikkTSpace). Missing normals are computed from the triangles.
