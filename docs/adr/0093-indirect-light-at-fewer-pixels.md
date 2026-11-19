# ADR-0093: Indirect light at fewer pixels

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.1 (ADR-0092). Indirect light traced a ray per pixel (ADR-0045) and was the frame's most expensive pass: 3.0 ms in the start room and 3.5 ms on the loading floor on High, at 1080p on an RTX 3090. Most engines trace diffuse light at a lower resolution and fill in the rest.

## Decision

- **`renderer.gi_pixels_per_ray`**, 1, 2 or 4. At 2, half the pixels trace, in a checkerboard; at 4, one of each two by two. The traced pixels take turns from frame to frame, so every pixel traces once in two or four frames.
- **A fill pass** gives the other pixels light, in this order:
  1. **The pixel's own light from last frame.** A history texture holds each pixel's last sample. The fill finds the pixel's point where last frame's view saw it, with last frame's view and projection, and takes the sample there if its distance from the camera matches, within 2%.
  2. **Its traced neighbours' light,** only where the history did not see the point (a surface coming into view, or one that moves). Their weight falls with screen distance, with distance from the pixel's plane and with the angle between normals.
- **Why the history comes first.** The first version filled every pixel from its neighbours. That spread each bright sample over the pixels around it, in blobs of four and five pixels, and the denoiser took that correlated noise for detail and kept it: ceilings and dark walls turned grainy. With the history, each pixel's noise is its own, which the denoiser filters as it filters a traced pixel's.
- **ReSTIR's candidates** become a setting, `renderer.restir_candidates`, 4 on Low and Medium (below).
- **The presets:** Low and Medium trace half the pixels. High and Ultra trace every pixel: High is the look the engine is tuned for, and while the camera moves, half the pixels leave a faint grain on dark ceilings, visible 16 times brighter than the game. A quarter is left as a setting: it is blotchy in motion.

## Measurements

At 1080p on an RTX 3090, dressed factory, flashlight on, same build, by pixels per ray. The indirect light pass includes the fill:

| Preset | Pixels per ray | Indirect light, start room / loading floor | Frame, start room / loading floor |
|---|---|---|---|
| Low | 1 | 0.22 / 0.40 ms | 3.8 / 4.8 ms |
| Low | 2 | 0.16 / 0.25 ms | 3.8 / 4.6 ms |
| Low | 4 | 0.13 / 0.17 ms | 3.8 / 4.5 ms |
| Medium | 1 | 1.44 / 1.73 ms | 6.5 / 7.7 ms |
| Medium | 2 | 0.82 / 0.92 ms | 5.8 / 6.8 ms |
| Medium | 4 | 0.50 / 0.52 ms | 5.4 / 6.5 ms |
| High | 1 | 3.21 / 3.65 ms | 13.0 / 14.6 ms |
| High | 2 | 1.76 / 1.99 ms | 11.7 / 12.8 ms |
| High | 4 | 0.98 / 1.05 ms | 10.1 / 11.8 ms |

Low gains little: it lights no surface along the ray (`gi_bounces 0`), so its rays were already cheap.

With both changes on Low and Medium, all presets in one run:

| Preset | Start room | Loading floor |
|---|---|---|
| Low | 3.5 ms (was 3.8) | 4.3 ms (was 4.8) |
| Medium | 5.6 ms (was 6.5) | 6.7 ms (was 7.7) |
| High | 13.1 ms | 15.6 ms |
| Ultra | 14.4 ms | 16.6 ms |

High and Ultra are unchanged; their run-to-run spread is about 1 ms. The first launch's shares of High (ADR-0052) become Low 0.28 and Medium 0.43, from 0.32 and 0.53. A GPU whose High frame takes 30 ms now gets Medium instead of Low.

Screenshots of the workshop, the west room and the loading floor, still and while turning, at the game's brightness: 1 and 2 cannot be told apart. 16 times brighter, 2 shows a faint grain on dark ceilings while the camera moves, and 4 is blotchy.

**ReSTIR's candidates** (direct light, ADR-0044). On High, four candidates per pixel instead of eight took direct lighting from 2.96 to 2.30 ms in the start room and from 2.89 to 2.52 ms on the loading floor. Screenshots of the three rooms, 16 times brighter, cannot be told apart: the temporal and spatial reuse make up the candidates each pixel no longer draws. Rooms with many lights depend on the candidates more, and the factory's rooms have few. **`renderer.restir_candidates`**, from 1 to 32, is now a setting: 4 on Low and Medium, 8 on High and Ultra.

## Consequences

- The fill reuses each sample over two or four frames, so indirect light follows a moving light one or three frames later.
- The denoiser's variance estimate sees repeated samples as less noise than they are. Telling it the number of pixels per ray, so that it blurs more, is left to step 18.5.
