# ADR-0045: Bounces lit before the cache, clamped bounces, and the dark corridor; ReSTIR GI measured and dropped

- **Status:** Accepted; pixels the denoiser has no history of may trace more rays (ADR-0121)
- **Date:** 2026-11-14
- **Amends:** ADR-0040 (each pixel's ray lights the surface it hits before it reads the cache)

## Context

Step 9.13 of the roadmap: the second half of the old step 9.12 (ADR-0041, ADR-0044). Each pixel's indirect light was one ray in a cosine-weighted direction that read the radiance cache where it hit (ADR-0040). The cache is coarse, 10 cm cells near the camera, and it follows a light by a moving average: the bounce lacked the shadows and detail of the surfaces it came from, and lagged a light that moved. The rendering plan (section 7.1) asked for ReSTIR GI (Ouyang et al. 2021): each pixel reuses the bounce samples of its own past and of its neighbours.

The spec's main risk is low light: a horror game shows most of its corridors lit only by the flashlight, where nearly everything seen is light bounced off a wall, and the engine has no Ray Reconstruction to clean it (ADR-0041). This step measures that on a dark corridor before anything builds on it.

The roadmap said two to three bounces before the cache. The cache already gives every later bounce; bounces before it only push its coarseness farther from the pixel, at the cost of rays.

Two changes to the plan were approved before the work. ReSTIR GI would be kept only if it beat the same bounces without it, at equal cost: one bounce lit by the lights, alone, was expected to give most of the gain, and comparing against the cache read at the first hit would credit ReSTIR with it. And the lights weighed at each bounce would be a setting of its own, measured, since ReSTIR DI had measured about 0.1 ms per light weighed per pixel at 1080p.

## Decision

### Bounces lit before the cache

- Each pixel's ray, where it hits, sends towards the pixel the light of one of the frame's lights, picked as everywhere else (ADR-0044) among `renderer.gi_light_candidates`, with one shadow ray; then its diffuse color times what one more cosine-weighted ray brings back. The last ray reads the cache, or the sky where it leaves.
- **`renderer.gi_bounces`**, from 0 to 3: the surfaces lit so before the cache is read. **1 by default.** 0 is ADR-0040's, the cache at the first hit.
- **`renderer.gi_light_candidates`**, from 1 to 8: **4 by default.** The cache's own paths keep eight.
- **`renderer.gi_rays`**, from 1 to 16: rays per pixel, averaged. **1 by default**; 2 fits the budget and is the first thing a higher quality preset (9.15) can buy.

### Clamping, on bounces only

- The light a ray brings back to a pixel is clamped in luminance, in pre-exposed units: **`renderer.gi_clamp`, 16 by default**, 0 for none. A lamp near a wall makes a small, very bright patch that one ray in thousands finds (ADR-0043); clamped, it stops spreading as fireflies.
- The cache's paths clamp what reaches each surface from the surfaces after it.
- Direct light, reflections of what is seen directly, and emission seen by the camera are never clamped.

### The dark corridor

A test scene built in code, as the Cornell box is (`renderer::MakeDarkCorridor`, and `sandbox.scene corridor`): a corridor 2 m wide, 3 m high and 20 m long, of mid-grey walls, with a doorway into a side room and a few boxes, one of them rusty. No light but the player's flashlight (`renderer::Flashlight`: 1,090 cd, 13 to 22 degrees, held in the left hand and aimed 8 m ahead), at the factory's EV100 of 3. The path tracer is its reference. Three scores, at 160 × 90:

- **FLIP** of the 32nd denoised frame after a cut;
- **flicker**: the mean change of luminance between denoised frames that do not move, over its mean;
- **lag**: after the view and the flashlight turn 30 degrees, the frames until the pixels lit only by bounces are, on average, within 10% of the path tracer's.

### ReSTIR GI: built, measured, dropped

ReSTIR GI was built as ADR-0045 proposed it: a reservoir of 32 bytes per pixel holding the point a ray found, its normal, color and light, the weight and the confidence; temporal reuse through the motion vectors, the history capped at 30, its sample lit again every frame; spatial reuse of four neighbours within 30 pixels, with the Jacobian of the change of point and a visibility ray from the pixel to each neighbour's point; the indirect light from the sample kept. It passed the test of the thin wall: the dark room stayed black, spatial reuse included. It was as bright as the path tracer, within 1%, and a single raw frame was much closer to it than one ray (FLIP 0.32 against 0.51).

After the denoiser, it was worse. ReSTIR's samples are correlated from frame to frame and between neighbours; the denoiser's temporal accumulation, built for noise that changes every frame (ADR-0039), no longer averages it out, and its spatial filter leaves blotches. A shorter history helps, monotonically, and never enough (dark corridor, denoised, one bounce):

| History cap | 30 | 10 | 4 | 1 | Without ReSTIR GI, 1 ray |
|---|---|---|---|---|---|
| FLIP | 0.267 | 0.236 | 0.187 | 0.127 | 0.112 |

It also costs more than the 1.5 ms budget: 2.24 ms at 1080p with one bounce, about what four rays cost. At equal cost, it loses on every score:

| Dark corridor, one bounce | GPU time at 1080p | FLIP | Flicker | Lag |
|---|---|---|---|---|
| ReSTIR GI | 2.24 ms | 0.267 | 0.0062 | 0 frames |
| 4 rays | 2.69 ms | 0.073 | 0.0045 | 0 frames |
| 2 rays | 1.34 ms | 0.087 | 0.0059 | 0 frames |
| 1 ray, the default | 0.66 ms | 0.112 | 0.0078 | 0 frames |

As the acceptance criterion said, the step keeps the bounces, the clamping and the corridor, and ReSTIR GI is not kept. Reuse of bounce samples comes back only with a denoiser built for correlated samples, which the plan does not hold.

## Measurements

On an RTX 3090.

Bounces before the cache, one ray, denoised, against the path tracer:

| Bounces | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| Cornell box | 0.157 | 0.116 | 0.114 | 0.118 |
| Mirror room | 0.268 | 0.214 | 0.217 | 0.217 |
| Dark corridor | 0.243 | 0.112 | 0.109 | 0.105 |
| Dark corridor, lag | 0 to 48 frames* | 0 | 3 | 18 |
| Indirect light at 1080p, corridor | 0.34 ms | 0.66 ms | 0.98 ms | 1.34 ms |

\* Read from the cache, the bounce-lit pixels hover around 10% from the path tracer's, in and out: the cache's coarse cells, not a delay. With more bounces lit, the lag comes back: the bounce farthest from the pixel reads cells the flashlight has just left.

The first bounce does nearly all of it: the light of the surface the ray hits, with its shadows, at once. Later bounces add little the cache does not already give, for a third of a millisecond each.

Light candidates at each bounce, in the room of a thousand lights of ADR-0044, denoised: 1, 0.181; 2, 0.146; **4, 0.131**; 8, 0.133.

Clamping, FLIP denoised: the dark corridor barely changes, 0.115 without a clamp and 0.125 at 0.5, its bounces being dim; the room of a thousand lights is 0.144 without a clamp, 0.143 at 16, 0.179 at 4, 0.41 at 0.5. 16 changes none of the reference scenes and cuts what is far brighter.

Other scenes, with the defaults, against ADR-0040 and ADR-0042's: the Cornell box 0.123 denoised (0.159); the mirror room 0.200 (0.246), with its glass 0.197 (0.244); half a second after a light goes off, 2.6% of its bounced light remains (4.8%).

In the factory, at 1920 × 1080 from the start: indirect light 1.19 ms (0.63 ms before), the radiance cache 0.34 ms, the frame 6.46 ms of GPU time (5.64 ms).

## Consequences

- Bounced light shows contact detail: a box's bounce shadow on the floor, the color of a lit wall on its neighbour, the flashlight's bounce moving with it.
- **Budget:** the cache and the indirect light take 1.53 ms in the factory at 1080p, at the budget of 1.5 ms set for this step.
- Specular reflections stay as in ADR-0042, diffuse indirect light is one or more independent rays per pixel, and the denoiser is unchanged.
- The dark corridor's scores are held by a test (`DarkCorridorTest`), with thresholds from these measurements, before 9.14 builds on its look.

## Alternatives considered

- **ReSTIR GI**, as proposed: see above.
- **Two to three bounces before the cache, fixed**, as the roadmap said: a third of a millisecond each for nothing the measurements show; a setting keeps them.
- **A history of one frame for ReSTIR GI**: spatial reuse only, nearly; still behind one ray after the denoiser, and still the cost of four.
- **A denser radiance cache**: finer cells cost memory everywhere, and fill slower; the bounce lit at the hit gives the detail where the pixel looks.
