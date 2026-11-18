# ADR-0087: The showcase's budget

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 16.6 closes the showcase (ADR-0082) by measuring it on every quality preset, fixing what can be fixed now, and listing the rest for Phase 17. Dressing the factory (ADR-0083) multiplied the triangles drawn by 80, and added about thirty lamps and the scares' lights.

## Measurements

GPU time per frame at 1080p on an RTX 3090 (`renderer.log_gpu_times`, 100 frames), in the start room and on the loading floor:

| Preset | Start room | Loading floor | Largest passes on the loading floor |
|---|---|---|---|
| Low | 3.9 ms | 4.9 ms | reflections 0.9, denoiser 0.7, G-buffer 0.7 |
| Medium | 6.7 ms | 8.1 ms | indirect 1.9, direct 1.3, reflections 1.3 |
| High | 13.6 ms | 15.1 ms | indirect 3.8, direct 2.9, reflections 2.6, denoiser 1.8, G-buffer 1.6 |
| Ultra | 19.3 ms | 23.7 ms | indirect 11.8, direct 2.8, reflections 2.5 |

Before the dressing, High took 11.6 ms in the start room with a G-buffer of 0.55 ms. The G-buffer now takes 1.6 to 1.7 ms. Direct light costs more by the lamps added.

## Decision

- **Frustum culling of rasterized draws.** The mesh pass rasterized every draw of the level, whatever the camera saw. A draw whose bounding sphere is wholly outside one side of the view is now left out of the G-buffer and the glass pass. Rays still meet it, in reflections, shadows and bounced light. In the factory it removes few draws: from the start room the view runs the length of the level, 416 draws of 483 stay, and the G-buffer goes from 1.63 to 1.57 ms. Its gain is in the views that look at a wall.
- **The first launch's measurement counts only frames of play.** It counted frames behind the main menu, where the scene is paused and renders much less: High measured 4.1 ms, and Ultra was chosen for a GPU that takes 24 ms for it on the loading floor. Frames of a paused scene now reset the measurement, and frames warm up again after it. Measured while playing, High takes 11.9 ms at the start, and High is chosen for 60 fps.
- **The presets' relative costs** that the choice uses are measured again in the dressed factory: Low 0.31 of High, Medium 0.52, Ultra 1.5. They were 0.36, 0.50 and 1.65 in step 9.18.

## For Phase 17

- **Occlusion culling.** Most of what is drawn from the start room is behind walls. Portals between rooms, which the level could declare at its doors, or occlusion against the previous frame's depth (Hi-Z), would remove most of it. Portal visibility was deferred in ADR-0073.
- **Level of detail.** ADR-0081's threshold for meshoptimizer is reached, with a G-buffer over 1.5 ms. Simplified meshes for distant props (`meshopt_simplify`), with the vertex cache and fetch ordered at import.
- **Ultra's indirect light** costs 9 to 12 ms, four times High's: its rays and bounces are worth measuring against the image they give.
- **Reflections on the loading floor** cost 2.5 ms on High, from the wet patches' puddles and polished props.

## Consequences

- On this GPU at 1080p, High holds 60 fps in the dressed factory, and Ultra does not.
- Players whose settings were chosen behind the menu by the old measurement keep them, until they choose again in the options or delete `user.cfg`.
