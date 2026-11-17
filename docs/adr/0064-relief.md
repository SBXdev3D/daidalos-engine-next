# ADR-0064: Relief from height maps

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 11 step 9 (ADR-0041, ADR-0046) asks for relief from the factory's height maps, which wait in `assets/Factory/Source/`: self-shadowing for direct light, so that the flashlight and the lamps reveal mortar and cracks, with parallax where it reads; and occlusion baked from the same maps into the red channel of the metallic-roughness texture, for indirect light, without darkening twice the albedos that already paint their cavities. Occlusion alone was ruled out: it does not move with the light.

## Decision

- **The height goes in the metallic-roughness texture's alpha, the occlusion baked from it in its red** (`texture-tool relief`): roughness in green and metalness in blue as before. The occlusion is the share of the sky the relief around each texel leaves open, over 16 directions, weighted by the cosine; the tool also prints how much the albedo already darkens where the relief occludes (the correlation of their luminance), and `convert_factory_textures.py` sets the material's occlusion strength to one minus it, at least 0.3: the factory's brick wall, whose albedo paints its mortar dark (0.68), takes a third of its occlusion; the vent slab, which paints none, all of it. Sets with relief become `*mrh.ktx2`; their material's glTF `occlusionTexture` is the same texture, and its extras say the relief's depth, `{"height": 0.012}` (meters, guessed per surface: mortar 12 mm, concrete 8, planks 6, pressed metal 4 to 6).
- **The G-buffer finds the relief's horizons** (`ReliefHorizons`): along 8 directions of a basis built from the surface's own normal, the steepest rise of the height within four times its depth, in 4 steps, at a coarser level than the pixel's and with even gradients so that anisotropic filtering takes one tap. Each horizon is the square root of its elevation's sine in 4 bits; the 32 bits go in a third word of the layers target, now `Rgba32Uint`. Relief that would span less than half a pixel is skipped.
- **Every light is shadowed by the relief** in `Respond`, the BSDF-times-cosine all direct lighting goes through (`ReliefShadow`): the light's azimuth in the same basis picks two horizons, and light from below them fades out over a soft edge. ReSTIR's choice of light, its shading and the fog's receivers see the same.

Measured on an RTX 3090 at 2560x1440 in the rain room, where relief covers the screen: the G-buffer takes 1.42 ms, against 0.71 ms without the horizons, 0.56 ms before the wider target (`renderer.log_gpu_times`). A first version, 6 steps at the pixel's level with anisotropic taps, took 5.1 ms. `ReliefShadowsGrazingLight`: a floor of ridges under a lamp low at its side returns 22% of the light it does flat.

## Consequences

- No parallax: the relief shadows and occludes, but its silhouette stays flat. Parallax occlusion mapping shifts the texture coordinates of the whole material, decals and wetness included, and reads only at grazing angles on deep relief, which the factory has little of.
- Rays (reflections, bounced light, the path tracer) see surfaces without relief; reflections of relief are flat.
- The relief's depth is a guess per surface, not a measure; it is one number in each material's extras to tune.
- The occlusion bake takes about 50 s per 1024² texture, once.

## Alternatives considered

- **Micro-shadowing from the occlusion alone** (a cone from the occlusion, as Uncharted 4 did): no horizons to store, but it shadows the same way from every side, which is what was ruled out.
- **Horizons baked into textures** (a horizon map per texture): no march in the G-buffer, but 8 more channels per texture, and the lighting would need each pixel's tangent frame to read them.
