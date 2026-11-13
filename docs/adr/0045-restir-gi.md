# ADR-0045: ReSTIR GI, clamped bounces, and the dark corridor

- **Status:** Proposed
- **Date:** 2026-11-13
- **Amends (when accepted):** ADR-0040 (the per-pixel ray becomes ReSTIR GI's first sample)

## Context

Step 9.13 of the roadmap: the second half of the old step 9.12 (ADR-0041, ADR-0044). Each pixel's indirect light is one ray in a cosine-weighted direction that reads the radiance cache where it hits (ADR-0040). The cache is coarse, 10 cm cells near the camera, so the bounce lacks the shadows and detail of the surfaces it comes from, and one ray per pixel is noisy where little light bounces. The rendering plan (section 7.1) asks for ReSTIR GI (Ouyang et al. 2021): each pixel reuses the bounce samples of its own past and of its neighbours.

The spec's main risk is low light: a horror game shows most of its corridors lit only by the flashlight, where nearly everything seen is light bounced off a wall, and the engine has no Ray Reconstruction to clean it (ADR-0041). This step measures that on a dark corridor before anything builds on it.

The roadmap said two to three bounces before the cache. The cache already gives every later bounce; bounces before it only push its coarseness farther from the pixel, at the cost of a ray each. How many are worth it is measured here.

## Decision

### The sample

- **The first sample** is today's ray: from the G-buffer's surface, in a cosine-weighted direction. Where it hits, the point sends towards the pixel:
  - the light of one light, picked as everywhere else (ADR-0044), with one shadow ray;
  - then, for each further bounce, its diffuse color times what one more cosine-weighted ray brings back; the last ray reads the cache, or the sky where it leaves.
- **`renderer.gi_bounces`**, from 0 to 3: the surfaces lit before the cache is read. 0 is today's, the cache at the first hit. The default is chosen from the measurements below; 1 is expected.
- A reservoir keeps the hit's position, normal and light, the weight and the sample count: 32 bytes, two buffers that swap each frame, about 133 MB at 1080p.

### Reuse

- **Target:** the luminance of the sample's light times the cosine at the pixel.
- **Temporal:** the same surface's reservoir from the previous frame, through the motion vectors with the denoiser's tests; sample count capped at 30.
- **The temporal sample's light is shaded again each frame** at its point, with a new light sample and its shadow ray: a flashlight moves with the camera, and a bounce that kept last frame's light would trail behind it. `renderer.gi_revalidate` sets every how many frames, 1 by default, if the cost asks for less.
- **Spatial:** four neighbours within 30 pixels, on the same surface; a neighbour's sample is moved to the pixel with the Jacobian of the change of point (Ouyang et al. 2021).
- **Spatial reuse always traces a visibility ray** from the pixel to the neighbour's hit. Without it, a sample from beyond a wall's corner lights the other side: the leak ADR-0040 forbids.
- **Output:** the indirect light per unit of diffuse color, which the lighting pass adds as today, and the denoiser filters.

### Clamping, on bounces only

- A sample's light is clamped in luminance, in pre-exposed units, before it is reused: `renderer.gi_clamp`, its default set from the measurements. A point light near a wall makes a small, very bright patch that one ray in thousands finds (ADR-0043); clamped, it stops spreading as fireflies.
- The cache's paths clamp what they carry back after their first surface.
- Direct light, reflections of what is seen directly, and emission seen by the camera are never clamped.

### The dark corridor

A test scene, built in code like the Cornell box: a corridor 2 m wide, 3 m high and 20 m long, mid-grey walls, a doorway into a side room, a few boxes, no light but a spot that follows the camera as the flashlight does, at a night-time EV100. The path tracer is its reference.

## Tests

- **GPU**:
  - the Cornell box and the mirror room against the path tracer, with 0 to 3 bounces;
  - **the dark corridor**, against the path tracer: the FLIP of the 32nd denoised frame; the flicker, the mean change of luminance between denoised frames when nothing moves; the lag, the frames until the bounce of a flashlight that turned 30 degrees is within 10% of the reference; each for 9.12's indirect light and for ReSTIR GI, at equal cost and at equal frames;
  - a wall 2 cm thick between a lit room and a dark one still leaves the dark room black, spatial reuse included;
  - a sample's light is clamped, direct light is not.
- The thresholds are set from the first measurement, then held by the tests, before 9.14 builds on the corridor's look.

## Acceptance

ReSTIR GI is kept only if, on the dark corridor, it beats 9.12's indirect light in FLIP and in flicker at equal cost. If it does not, the step keeps the clamping, the bounces before the cache and the corridor test, and records why.

## Consequences (to be measured)

- **Budget:** under 1.5 ms at 1080p on the RTX 3090 with one bounce, revalidation and spatial reuse.
- Contact detail in bounced light: a box's bounce shadow on the floor, the color of a lit wall on its neighbour.
- Memory: about 133 MB of reservoirs at 1080p.
- Specular reflections stay as in ADR-0042: ReSTIR GI is diffuse. Glossy reuse waits for content that needs it.

## Alternatives considered

- **Two to three bounces before the cache, fixed**, as the roadmap said: rays spent where the cache already answers; a setting and a measurement decide.
- **Revalidating every few frames**, as RTXDI does: cheaper, and the flashlight's bounce would trail it in the scene that matters most.
- **Spatial reuse without visibility rays**: leaks through thin walls.
- **A denser radiance cache instead of ReSTIR GI**: finer cells cost memory everywhere, and fill slower.
