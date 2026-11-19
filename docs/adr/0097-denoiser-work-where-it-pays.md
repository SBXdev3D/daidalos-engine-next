# ADR-0097: The denoiser works only where it pays

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.5 (ADR-0092). The denoiser (ADR-0039) took 0.66 ms on Low, 0.86 to 0.90 ms on Medium and 1.7 to 2.3 ms on High and Ultra, at 1080p on an RTX 3090. Timed pass by pass on High:

| Pass | Start room | Loading floor |
|---|---|---|
| Temporal accumulation | 0.43 ms | 0.36 ms |
| Spatial filter, 1 pixel apart | 0.91 ms | 0.71 ms |
| Spatial filter, 2 pixels apart | 0.29 ms | 0.31 ms |
| Spatial filter, 4 pixels apart | 0.27 ms | 0.31 ms |
| Spatial filter, 8 and 16 pixels apart | about 0.1 ms together | |

Three findings:

1. **The last two iterations never filtered anything.** A pixel takes part while step² × frames ≤ 32, and a pixel always has at least one frame: at 8 pixels apart, 64 > 32. ADR-0039 says a pixel just uncovered gets "the widest three"; it got 1, 2 and 4. The two passes only copied the image.
2. **Most of what the 4-pixel iteration still filtered was history thrown away for nothing.** A debug image of the pixels it filtered, the camera standing still, showed every grout line of the brick walls and specks all over the ceiling and the floor. The temporal pass keeps last frame's light only where last frame's normal is within 25° of this one's. The G-buffer's normals include the normal maps, and the camera's jitter samples a different texel of them each frame: on grout and rough textures, the normal turns by more than 25° from one frame to the next, so those pixels started over every frame. Their light stayed as noisy as one frame's, and a group of 8 × 8 threads with one such pixel ran the whole filter.
3. **Indirect light and rough reflections at fewer pixels** (ADR-0093, ADR-0096) bring a sample forward for two or four frames. The temporal pass counted each frame as a new sample, so it took the average for less noisy than it was, and blurred it too little.

## Decision

- **Three iterations,** 1, 2 and 4 pixels apart (`Denoiser::kIterations`). The image is the same.
- **History kept within 60° of the normal** (`kNormalTolerance` 0.5, was 0.9). The depth test, 5% of the distance, still keeps the history on the same surface; a crease of 90° still starts over. In the debug image, the grout lines and the specks are gone; what the wide iteration still filters is geometric edges and the pool of a swinging lamp, whose light moved.
- **The denoiser knows how often a sample repeats** (`DenoiserInputs::diffuseRepeats` and `specularRepeats`, the indirect light's and the reflections' pixels per ray). The variance of the average is divided by the samples it holds, frames ÷ repeats, and the spatial filter's step test counts samples, not frames, for each kind of light. Diffuse light repeats on Low and Medium; rough reflections from Medium up.
- **The spatial filter leaves alone what it would give back unchanged:** light whose noise, one standard deviation, is below 0.5% of its luminance (`kQuietNoise`), and specular light on surfaces too smooth for it to take neighbours (roughness below 0.1). A pixel with neither to filter is copied, and a neighbour's light is read only for the kinds being filtered.

## Measurements

At 1080p on an RTX 3090, dressed factory, the old and the new build in turns. The Denoiser group, two runs each (five on Ultra):

| Preset | Start room | Loading floor |
|---|---|---|
| Low | 0.66 → 0.51 to 0.54 ms | 0.66 → 0.54 to 0.56 ms |
| Medium | 0.85 to 0.86 → 0.65 to 0.66 ms | 0.90 to 0.91 → 0.81 to 0.82 ms |
| High | 1.76 to 2.37 → 1.46 to 2.03 ms | 1.75 to 1.78 → 1.56 to 1.57 ms |
| Ultra | 1.66 to 1.93 → 1.42 to 2.11 ms | 1.74 to 2.27 → 1.56 to 1.58 ms |

The start room on High and Ultra runs in one of two states, about 0.5 ms apart, with either build; within a state, the new build is 0.2 to 0.3 ms faster. The frame, in all, takes 0.1 to 0.4 ms less.

The quiet-noise test alone took the 1-pixel iteration on High from 0.91 to 0.75 ms in the start room and changed nothing on the loading floor: most pixels there are noisy enough to filter.

**The image:** screenshots of the start room and the loading floor on Medium and High, at the game's brightness and 16 times brighter, old against new. The start room differs by more than 24 levels in under 1.5% of the pixels; the loading floor's differences are its rotating alarm beams, caught at another angle. Frames of the opening flight, the camera moving, 16 times brighter on Medium and High: no trail or smear at edges, the floors and ceilings as clean. The denoiser's GPU tests and the path tracer comparisons pass.

## Consequences

- Surfaces whose normal map turns sharply keep their history: their light is smoother, from more frames, as the rest of the wall's.
- History can now cross a bevel of up to 60° on the same plane within 5% of depth: a rounded edge lit differently on each side may lag a frame or two more there.
- The spatial filter's cost is set by edges and by pixels whose light moved. A list of the 8 × 8 tiles that need the wide iterations, dispatched indirectly, would skip the rest; it was not tried, since the two wide passes now cost about 0.25 ms together.

## Alternatives considered

- **Comparing the geometric normal instead of the shaded one:** the G-buffer does not store it, and a target for it costs more than a looser angle with the depth test.
- **A shared-memory tile for the 1-pixel iteration,** which reads each texel 25 times: left for when the denoiser's share of the frame matters more; it is now 1.5 ms of 12 on High.
