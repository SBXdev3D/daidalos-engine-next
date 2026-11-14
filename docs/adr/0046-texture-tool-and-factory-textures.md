# ADR-0046: The texture tool, and the factory's new textures

- **Status:** Accepted
- **Date:** 2026-11-14
- **Implements:** ADR-0010 (KTX2 in UASTC, transcoded to BC7, and BC5 for normal maps)

## Context

The owner provided new textures for the factory before Phase 9.14: an albedo, a normal map, a roughness map and a height map at 2048 × 2048 for eleven surfaces (ceiling, three crates, door, floor, lift, lockers, vent slab, wall, zombie), and a metalness map for the lift. The level had 512 × 512 albedos and normal maps, and one roughness and metalness per material, kept from the legacy engine (ADR-0042).

Three things stood in the way:

- **Memory.** PNG and JPEG are decoded to RGBA8 with their mip levels: 22 MB per 2048 × 2048 texture, about 740 MB for the 33 textures the materials use. ADR-0010 chose KTX2 for this, and the loader reads it, but nothing wrote it.
- **Roughness.** glTF has no roughness-only texture: green holds roughness and blue metalness, in one texture.
- **Height.** The engine has no use for a height map. Parallax occlusion mapping would shift the texture on screen only: shadow, reflection and bounce rays would still meet the flat triangle, and the relief would contradict the ray-traced light.

The vent slab's albedo was overwritten by its height map when the files were copied in. The legacy albedo stays in its place.

## Decision

### Encoding

- **`resources::EncodeKtx2`**, next to the decoder, so that tests check each file against what the loader makes of it. It writes UASTC from the image's own mip levels (`GenerateMips`, sRGB-correct), supercompressed with Zstandard at level 18. It uses libktx's encoder, which the vcpkg port already builds: no new dependency.
- **Rate-distortion optimization** for colors and data, not for normal maps, where it bends normals. Normal maps are tuned with libktx's `normalMap`, and come back as BC5 with red and green intact (a test checks the channels).
- **`texture-tool`**, a development app outside the release archive: `color`, `normal`, `data`, and `metallic-roughness <roughness> [<metallic>]`, which packs grayscale maps as glTF reads them. Without a metalness map, blue is full and the material's `metallicFactor` alone decides. `--compare` reads a KTX2 file back and reports its error against its sources.
- **`renderer.debug_view normals`** shows the G-buffer's normals, normal maps included: the factory's look the same with the PNG sources and with the KTX2 files, 1/255 apart on average.

### The factory

- **Sources** live in `assets/Factory/Source/`, under the legacy names (`wall_2d.png`, `door_n.png`, `lift_m.png`, `*_h.png`). The release archive leaves the directory out.
- **`scripts/convert_factory_textures.py`** encodes each set to `<name>d.ktx2`, `<name>n.ktx2` and `<name>mr.ktx2` beside the glTF files, skipping outputs newer than their sources, and points the listed materials at them through `KHR_texture_basisu`, which the files then require. Their `roughnessFactor` becomes 1, and the lift's `metallicFactor` too, since its map decides. Textures no material uses leave the files. The red door keeps its own albedo, with the door's normal and roughness maps.
- **Height maps** are kept as sources and not used. They wait for Phase 11 (deliverable 9): self-shadowing for direct light, and occlusion baked from them for indirect light. Occlusion alone was considered and left out: the engine applies it to indirect light only, the flashlight and lamps would not change, and the wall's albedo already paints its mortar near black. No source texture has an authored occlusion map.
- **Normal maps** are in the OpenGL convention, green up, as glTF expects: no flip.
- **Plain git**, not Git LFS: 177 MB of sources and 92 MB of KTX2.

## Consequences

- The 33 textures take 185 MB of video memory in BC7 and BC5, against 740 MB as RGBA8. The previous 512 × 512 textures took about 40 MB.
- Quality, measured with `texture-tool --compare` on the decoded UASTC at full size: albedos 36 to 46 dB PSNR (vent slab, the legacy 512 px albedo, lowest), metallic-roughness 44 to 58 dB, normal maps 0.4 to 2.5 degrees from the source on average (the wall's, full of fine grain, worst; up to 44 degrees on a few steep bevel texels). UASTC's slowest level brings the wall's to 2.4 degrees for ten times the time: not worth it. A render of the factory with PNG albedos and normal maps differs from the KTX2 one by less than its own lighting noise.
- Encoding takes 6 s (normal map) to 20 s (albedo) per texture on 16 threads: 7 minutes for the factory. It runs only when a source changes.
- `import_legacy_level.py` writes the legacy materials and PNGs back. The conversion script must run after it.
- Engine code that loads a PNG still decodes it at load; other content moves to KTX2 the same way, with the tool.
