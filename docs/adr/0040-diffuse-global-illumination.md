# ADR-0040: Diffuse global illumination with an own radiance cache, and directional lights

- **Status:** Accepted, amended by ADR-0045 (each pixel's ray lights the surface it hits before it reads the cache)
- **Date:** 2026-11-11
- **Supersedes:** ADR-0024's ambient occlusion and ambient light (the depth pre-pass stays)

## Context

Step 9.9 of ADR-0028. Light reached a surface two ways: from the lights, with ray-traced shadows, and from an ambient term, brighter from above than from below, darkened in creases by screen-space ambient occlusion. The ambient term is light from nowhere: it lights a closed room with no lamp, and it never takes the color of the walls around. The plan (`docs/daidalos-engine-next-rendering-plan.md`, section 7.1) asks for fully dynamic, multi-bounce indirect light with no leaks through walls. NVIDIA's SHaRC, the cache it names, is under the NVIDIA RTX SDKs licence, excluded by ADR-0028: the engine writes its own, from the published algorithms.

The owner also asked for moonlight in the big room in front of the player, from the moon painted on the level's sky. The engine had point and spot lights only.

## Decision

**Indirect light**, in three compute passes after the pre-pass, in `GlobalIllumination`:

- **A radiance cache.** A hash grid of world space, 2^20 cells (about 56 MB): the cell of a point comes from its position, quantized to cells of 10 cm near the camera that double in size with distance (4% of it), and from which of the six axis directions its normal is nearest. A cell is found from a 32-bit hash among 8 slots, and told from others by a second, independent 32-bit checksum: as good as a 64-bit key, with 32-bit atomics only, so no new device feature is required.
- **Update paths.** One path from the camera per 4×4 pixels, through a random point of its tile. At each of up to three surfaces it gathers emission and the light of one light, chosen among eight random candidates by their unshadowed light (resampled importance sampling, Talbot et al. 2005), with one shadow ray. Then it goes on in a cosine-weighted direction. After the third surface, its last ray ends with the light the cache already holds where it hits: light bounces frame after frame, without limit. Walking back, each surface's cell receives the light it sends along the path, added in fixed point with atomics.
- **A resolve pass** blends each cell's new samples with what it holds, as a moving average weighted by samples whose older ones weigh less every frame, whether paths went through the cell or not: the cache forgets in about 8 frames. A cell no path visited for 120 frames is freed.
- **One ray per pixel**, from the pre-pass's surface in a cosine-weighted direction, returns the light the cache holds where it hits, or the sky where it leaves. That is the pixel's indirect light per unit of diffuse color. The mesh pass adds it to its diffuse light, so the denoiser of ADR-0039 filters it unchanged.

**No leaks.** The cache is read where rays hit, so each face of a wall reads its own cells, in its own orientation. A wall thinner than a cell still lets the floor on either side share one: each cell keeps an anchor, the first point that made it, 1 cm off its surface. A point that cannot see the anchor (one short ray) uses the cell's second variant, another checksum in the same slots. A wall 2 cm thick between a lit room and a dark one leaves the dark room exactly black.

**Deleted:** screen-space ambient occlusion (`AmbientOcclusionPass`, `renderer.ambient_occlusion`) and `Atmosphere::ambient` / `ambientBelow`. No light comes from nowhere. The material's occlusion texture darkens the indirect light. `renderer.indirect_light` turns it off, for comparison: direct light only.

**Until reflections (9.10):** the indirect diffuse light also lights the specular albedo, as if every direction sent the same light. Particles take the indirect light of the surfaces behind them, averaged over a few pixels.

**Directional lights.** `LightType::Directional` shines along its direction everywhere, with an angular radius that softens its shadows. It reaches every light cluster. Shaders turn it into a light 10 km away, as wide as its disk, so that shading and shadow rays are the same for every kind of light, in the mesh pass, the path tracer and the cache.

**The moon.** `Atmosphere::skyYaw` turns the sky about +Y. The moon on `sky1` is 22° above the horizon, towards −Z from the start. That low, its light could only reach walls the player sees from behind as they enter the big room. The factory turns the sky half a turn and adds a directional light from where the moon is then painted: it lights the upper part of the far wall in front of the player, and that light bounces into the room.

**The factory's darkness.** `sandbox.darkness` used to blend the ambient light towards white; there is none now. Darkness 0 now raises `Atmosphere::exposure`, a new multiplier of the renderer's exposure, by up to 16 times, and drops the fog. `sandbox.ambient` is gone.

## Consequences

- At 2560×1440 on the RTX 3090, the factory takes 5.2 ms of GPU time with indirect light and 3.9 ms without: about 1.3 ms for the three passes and the extra denoising, near 0.75 ms at 1080p.
- The Cornell box, averaged over 128 frames, has 0.995 of the brightness of the path tracer at 8 bounces, and a FLIP error of 0.145 against 0.59 for direct light alone.
- The factory is much darker. The ambient term lit it everywhere, and most of the level had no lamp: the start room had none at all. Auto-exposure (9.12) adapts to the dark, but cannot show what no light reaches, so the level got lights where it should be seen, kept discreet: pools of light along the way, not a lit level. The start room's lamp, off in the legacy level, is on, and 12 dim ceiling lamps (0.5, warm, 45°) line the route: the corridor from the start, both roofed ends of the hall, the corridor across it, the east room, the low corridor past the red door and the north corridor. The big room's open middle keeps only its alarms and the moon.
- The cache needs a few frames to fill, and after a cut it starts over. Indirect light lags a moving light by a few frames, more where surfaces reflect most of it: half a second after a light goes off, 5% of its bounced light remains in a room of mid-grey walls.
- Surfaces at grazing distance from each other, under 1 cm, share their anchor test and can still share a cell.
- ReSTIR GI (9.11) can replace the per-pixel ray and reuse the cache as the end of its paths.

## Alternatives considered

- **Probe grids (DDGI):** the plan rules them out as primary GI: they leak through thin walls, and need visibility data per probe.
- **64-bit keys with 64-bit atomics:** the plan's first idea. Two 32-bit hashes give the same collision rate without requiring `shaderBufferInt64Atomics`.
- **A spot light over the opening instead of a directional moon:** cheaper, but its direction and shadows would not match the moon seen in the sky, and indirect light would bounce a light that is not there.
- **Keeping a little ambient light as a fill:** contradicts ADR-0028's fully dynamic lighting, and hides where the level lacks lights.
