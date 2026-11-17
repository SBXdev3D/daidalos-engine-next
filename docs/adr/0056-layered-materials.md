# ADR-0056: Layered materials: clear coat and sheen

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 1 (ADR-0041) asks for layered materials read from glTF's `KHR_materials_*` extensions, working the same in the G-buffer's lighting, at ray hits and in the path tracer, through the one BSDF (ADR-0028, ADR-0034). Glass (transmission, thick and thin) and emissive strength exist already (ADR-0042, ADR-0043). What a dark industrial horror game needs most among the rest: varnished and painted surfaces (wet-looking floors, lacquered doors, car paint), which a clear coat gives, and cloth, felt and dust, which a sheen gives. Wetness (step 11.6) will put a clear coat of water on any surface.

## Decision

- **Two layers over the base, in `Bsdf.slang`:** a clear coat, a GGX dielectric of index 1.5 with its own roughness, which reflects first and dims what is below by its Fresnel (the glTF extension's model, with the coat's Fresnel taken at the larger of the view's and the light's cosines so that energy is not created at grazing light); and a sheen, the Charlie distribution (Estevez and Kulla 2017) with Neubelt's visibility, which dims the base by a fitted albedo. `EvaluateLayeredTerms` adds both to the specular part of `BsdfTerms`, which the lighting, ReSTIR's target function, the reflections and the path tracer all use; `SampleLayeredBsdf` picks the coat's lobe with about the probability of what it reflects, else the base's lobes, and weighs by the density of the whole mixture, so the path tracer stays unbiased.
- **The G-buffer keeps the layers in one more target, 64 bits per pixel** (`Rg32Uint`): eight bits each for the coat, its roughness, the sheen's roughness and the sheen's color. Exact colors, one target, no change to the others' layouts. The RHI's limit of color targets goes from 4 to 8, which every GPU with ray tracing has.
- **Materials carry the factors**, from `KHR_materials_clearcoat` and `KHR_materials_sheen` (`MaterialDesc::clearcoat`, `clearcoatRoughness`, `sheenColor`, `sheenRoughness`). The GPU material grows from 96 to 128 bytes.
- **The lighting's specular albedo counts the layers** (`LayeredSpecularAlbedo`), since the specular light is divided by it before the denoiser and multiplied by it after (ADR-0039).
- **A debug view, `renderer.debug_view layers`:** the coat in red, the sheen in green.
- **Lighting runs without layers when given none** (`DirectLightingInputs::layers` empty): tests and tools that build their own G-buffer keep working.

Measured on an RTX 3090 (`ClearCoatAndSheenMatchThePathTracer`): two spheres, a coated red one and a sheened cloth, under a point light. The lighting's image is 0.014 FLIP from one bounce of the path tracer; without the layers, 0.116. A coated white cube in a white sky keeps its energy in the path tracer (`ClearCoatKeepsItsEnergyInAWhiteSky`).

## Consequences

- The real-time reflections (ADR-0042) follow the base's roughness: a smooth coat over a rough base shows its sharp highlights from lights, and the path tracer shows its sharp reflection of the room, but the denoised reflections show the base's blurred one. Tracing the coat's lobe as a second reflection, or choosing between the two lobes per pixel, is the next step when a coated surface needs it (the wet floors of step 11.6 will).
- Textures for the coat and the sheen, the coat's own normal map, anisotropy, thin film and `KHR_materials_specular` are not read: their factors are. They come back when an asset needs them; the G-buffer target has the room (two of its 64 bits are free for flags, and anisotropy would need a direction).
- The sheen's albedo is a fit, not the tabulated one: the base loses a little more or less light than it should at grazing angles. The lighting and the path tracer use the same fit, so they agree with each other.

## Alternatives considered

- **OpenPBR's full layering** (fuzz, coat with its own normal and absorption, thin film, specular weight): the target in ADR-0041's words. Its full evaluation costs much more per light and needs more G-buffer data; the two layers kept are the ones the factory can show. The rest is a later step on the same functions.
- **Packing the layers into spare bits of the existing targets:** the base color's alpha holds the metalness, the normals' alpha the roughness, the emission's alpha the occlusion. Nothing is spare without losing precision somewhere visible.
- **A sheen color reduced to its brightness:** one 32-bit target, but cloth would lose its tint in the lighting and keep it in the path tracer.
