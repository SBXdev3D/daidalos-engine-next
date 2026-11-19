# ADR-0096: Half the rough reflections

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.4 (ADR-0092). Reflections took 2.3 to 2.5 ms on the loading floor on High, at 1080p on an RTX 3090, and 0.3 ms in the start room. Each pixel smoother than `renderer.reflection_roughness` casts one ray along its specular lobe (ADR-0042):

- below roughness 0.3 (`kSharpRoughness`), the ray shades what it hits fully;
- above it, the ray reads the radiance cache where it hits.

Lowering the limit showed where the time goes on the loading floor:

| Rays traced for roughness | Reflections |
|---|---|
| up to 0.6 (High) | 2.29 ms |
| up to 0.3 | 1.19 ms |
| up to 0.05 | 0.32 ms |

The rough reflections, from 0.3 to 0.6, cost half the pass: about 1.1 ms. Their reflection is a blur, which changes little from one pixel or frame to the next.

## Decision

- **`renderer.reflection_pixels_per_ray`**, 1 or 2. At 2, half the rough pixels trace their reflection each frame, in a checkerboard that takes turns, as indirect light does at fewer pixels (ADR-0093). Sharp reflections trace every pixel.
- **A fill pass** (`GiReflectFill.slang`) gives each other rough pixel its reflection:
  1. **its own from last frame:** a history of reflections, found with last frame's view and checked by distance;
  2. **or, where last frame did not see the point,** its traced neighbours' on the same surface.

  Its hit distance, which the denoiser follows reflections with, is its neighbours'. The neighbour filter and the history are shared with indirect light's fill (`GiFill.slangh`).
- **Two launches, a thread per pixel of each half of the checkerboard.**
  - The half whose turn it is traces every reflection.
  - The other half traces only the sharp ones, so that its groups on rough surfaces end at once.
  - The first version skipped the rough pixels of the other half inside one launch, and saved nothing (2.48 against 2.50 ms). A group of threads lasts as long as its slowest thread, and every group still held tracing pixels.
- **The presets:** Medium, High and Ultra trace half the rough reflections. Low traces only sharp ones (`reflection_roughness 0.3`), so there is nothing to halve, and the fill would cost alone.

## Measurements

At 1080p on an RTX 3090, dressed factory, flashlight on, 1 and 2 in turns, two runs each. The Reflections pass, fill included:

| Preset | Start room | Loading floor |
|---|---|---|
| Medium | 0.15 to 0.17 → 0.18 to 0.21 ms | 1.13 to 1.18 → 1.02 ms |
| High | 0.31 to 0.36 → 0.36 to 0.46 ms | 2.43 to 2.54 → 2.13 ms |
| Ultra | 0.33 to 0.37 → 0.41 to 0.42 ms | 2.44 to 2.50 → 2.18 to 2.19 ms |

On the loading floor, the busiest view, the pass takes 0.1 to 0.35 ms less. In the start room, where few surfaces are rough enough, the fill and the second launch cost 0.03 to 0.1 ms more. The frame's budget is set by the busy views.

**The image:**

- **A GPU test** (`HalfTheRoughReflectionsMatchThePathTracer`): a rough floor and a rough block, averaged over 64 frames. FLIP against the path tracer is 0.0176 with half the pixels traced, against 0.0171 with every pixel.
- **Screenshots, 16 times brighter:** the loading floor, the west room and the workshop, still. The loading floor also during the opening's flight, the camera moving. The wet floor and the tiles cannot be told apart at full size.

## Consequences

- A rough reflection follows a moving light or object one frame late, on the pixels that did not trace.
- The history's two textures hold 33 MB at 1080p (half floats, four channels).
- Sharp reflections, about 0.9 ms of the loading floor's pass, and water, about 0.3 ms, are not touched: their reflections show detail that a neighbour's or last frame's would blur.

## Alternatives considered

- **Half resolution for every reflection:** more saving, but mirrors and puddles would lose their sharpness, which the factory's wet floors show the most.
- **Skipping rough pixels within one launch:** measured, no saving (above).
- **A quarter of the rough pixels, as indirect light allows:** not tried. The fill brings forward three frames out of four, and a moving light would lag that much longer.
