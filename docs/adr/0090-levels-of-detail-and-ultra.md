# ADR-0090: Levels of detail measured, Ultra's indirect light halved

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Steps 17.2 and 17.3 (ADR-0088). ADR-0081 had decided that meshoptimizer would come when the G-buffer passed 1.5 ms, which the dressed factory did (ADR-0087). Ultra cost 1.5 times High, and its indirect light four times High's.

## Decision

- **No levels of detail yet.** With portal visibility (ADR-0089), the G-buffer takes 1.30 ms from the start room, for 53,000 triangles, and 1.35 ms on the loading floor, for 583,000. Half a million triangles cost 0.05 ms on an RTX 3090. Simplified meshes would save less than the time to choose them. The G-buffer's cost is per pixel: materials, decals, wetness and relief. ADR-0081's threshold was on the wrong quantity. Levels of detail come when triangles in view, not the G-buffer's time, reach the millions, or on a GPU where they cost more. Rays keep full meshes anyway.
- **Ultra's indirect light** at 1080p on an RTX 3090:

  | Ultra's indirect light | Start room | Loading floor |
  |---|---|---|
  | Two rays, two bounces | 8.5 ms | 10.8 ms |
  | One ray, two bounces | 4.3 ms | 5.6 ms |
  | Two rays, one bounce | 4.9 ms | 7.4 ms |

  Screenshots of the workshop with one ray and with two cannot be told apart: the second ray lowers noise that the denoiser removes anyway. Ultra keeps the second bounce and drops the second ray.
- **The presets' relative costs** are measured again for the first launch's choice (ADR-0052, ADR-0087): Low 0.32 of High, Medium 0.53, Ultra 1.2.

## Measurements after this step

Frame time at 1080p on an RTX 3090, dressed factory with its visibility cells:

| Preset | Start room | Loading floor |
|---|---|---|
| Low | 3.7 ms | 4.7 ms |
| Medium | 6.4 ms | 7.6 ms |
| High | 11.7 ms | 15.0 ms |
| Ultra | 14.6 ms | 16.9 ms |

Ultra was 19.3 and 23.7 ms at the start of 17.1.

## Consequences

- Ultra nearly holds 60 fps on this GPU, and the first launch still chooses High for it, with the margin of ADR-0052.
- Reflections on the loading floor (2.5 ms on High) remain the most expensive pass that the dressing added. Its cost comes from rough wet surfaces, which reflections trace up to roughness 0.6. That is a choice of look, left as it is.
