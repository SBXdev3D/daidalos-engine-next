# ADR-0049: Temporal upscaling and dynamic resolution

- **Status:** Proposed
- **Date:** 2026-11-14
- **Amends:** ADR-0037 (the resolve becomes an upscaler; sharpening is the engine's own), ADR-0048 (the test that decides whether FSR 3.1 is added)

## Context

Step 9.15 of the roadmap (ADR-0041, ADR-0048). Every pass renders at the window's size. The spec's targets render below it: 58 to 67% on the minimum tier at 1080p, 67% on the recommended tier at 1440p, with the resolution moving to hold 60 fps. ADR-0048 chose the engine's own temporal upscaling first, grown from the temporal antialiasing of ADR-0037, with FSR 3.1 added from source only "if TAAU at 67% loses against native resolution".

That test cannot fail the right way: any upscaler at 67% loses against native resolution, FSR 3.1 included. It would always add FSR.

Motion vectors are already in texture coordinates (ADR-0037), so that a lower render resolution changes nothing upstream of them. But about twenty shaders turn pixels into texture coordinates with the size of their targets, and four passes keep a history per pixel: ReSTIR DI's reservoirs, the denoiser, the indirect light's and the resolve's.

On the RTX 3090, the factory takes 6.5 ms of GPU time at 1080p: dynamic resolution would never act there. The owner's display is 1440p.

## Decision

### Render and output resolution

- **Render resolution**: everything up to the glass and the particles, the G-buffer, direct and indirect light, the denoiser, the composite, the motion vectors and the reactive mask.
- **Output resolution**, the window's: the upscaler and its history, the exposure meter, bloom, the output pass and the interface layer.
- **The targets of the render resolution are allocated at the output's size, and a frame draws into their top-left corner.** A change of resolution reallocates nothing and keeps every history: the cache is in world space, and the passes that look back find last frame's pixel with last frame's render size. Memory does not shrink with the scale.
- **`FrameData` gains** the render size, last frame's render size and the scale from render texture coordinates to the targets' (`renderScale`, the render size over the allocated size). Loads by pixel are unchanged. Filtered reads multiply their coordinates by the scale and clamp them half a texel inside the drawn corner, so that they never read what an earlier, larger frame left outside it. Compute passes dispatch over the render size.
- **Debug views and the path tracer render at 100%**, as they skip the resolve.

### The upscaler

- **`TemporalResolvePass` becomes the upscaler**, one compute pass at output resolution, for every scale; at 100% it is the temporal antialiasing it was. Its inputs are those FSR takes (color, depth, motion, reactive mask, jitter, the two sizes), so that FSR could replace it.
- **Per output pixel:** the render pixel nearest to it, and its jittered sample's distance to the output pixel's center; the sample's weight falls with that distance (a Gaussian of about one output pixel), so that a sample counts most where it landed. The neighbourhood box and the nearest depth's motion come from the 3 × 3 render pixels, as before. The history, at output resolution, is clipped to the box; where the new sample weighs little, the history keeps more, up to the ten frames it holds at 100%.
- **The jitter's cycle grows with the scale**: Halton (2, 3) over 8 / scale² points, 18 at 67%, so that each output pixel is covered.
- **Textures are sampled with a mip bias of log2(scale)** in the G-buffer, so that they keep the output's detail. The RHI's samplers have no bias; the G-buffer's one material read takes it from `FrameData`.
- **Sharpening**, which ADR-0037 left to FSR: a contrast-adaptive sharpening as FSR's RCAS does it (five taps, limited by the neighbourhood's contrast so that it does not ring), applied in the output pass, so that the history is never sharpened. `renderer.sharpness`, from 0 to 1, chosen from screenshots.

### Dynamic resolution

- **On the CPU**, as auto-exposure is (ADR-0048), and tested there: the GPU time of the frame that comes back, `kFramesInFlight` frames later, from the timestamps the statistics already read.
- **Down at once, up slowly**: over the budget, the scale drops to what the budget allows, assuming the cost follows the pixels; under 85% of it for half a second, it rises by a step. Steps of 2.5% of the scale, so that it does not hunt.
- **Settings:** `renderer.render_scale` (1.0 by default): the scale when dynamic resolution is off, its ceiling when it is on. `renderer.dynamic_resolution` (on). `renderer.min_render_scale` (0.5). `renderer.target_fps` (30, 60 or 120; 60): the GPU's budget is 90% of its frame time. The menu offers Native, Quality (67%), Balanced (58%) and Performance (50%), and dynamic resolution on or off. The statistics show the render size.

### Whether FSR 3.1 is added

ADR-0048's test is replaced. **Reference**: the same view rendered at twice the width and height with temporal antialiasing, averaged down to the output's size. Native temporal antialiasing and the upscaler at 67% are compared with it by FLIP, still and along a scripted camera pan in the factory and the dark corridor, and by flicker on thin geometry. **FSR 3.1 is added, in a step of its own, only if the upscaler's FLIP exceeds 1.15 times native resolution's, or screenshots show ghosting or shimmer that native resolution does not.** This ADR records the measurements.

## Tests

- **Unit:** the jitter's cycle per scale; the controller's descent at once, its slow ascent, its bounds and its hold, from timings given to it.
- **GPU:** the upscaler at 100% still passes the resolve's tests; at 67%, an edge converges to its coverage at output resolution and a jumping square leaves no ghost; the denoiser's, ReSTIR DI's and the indirect light's tests also run at 67%; a scale that changes every frame keeps the histories (no reset, no validation error) and reads nothing outside the drawn corner.
- **The measurements above**, as GPU tests that record their FLIP.

## Consequences

- Scales below 100% cost the passes at render resolution about the share of pixels they keep: at 67%, 45%.
- The targets at render resolution keep the output's memory at every scale.
- Every pass that turns pixels into texture coordinates uses the render size: a new pass that uses its target's size instead is wrong below 100%, and the tests at 67% are there to catch it.
- Dynamic resolution is checked on the RTX 3090 with a lowered budget, as the factory never exceeds it at 1080p or 1440p.

## Alternatives considered

- **Reallocating the targets when the scale changes**: memory follows the scale, but each change is a stall and resets every history.
- **A few fixed resolutions, each allocated**: no stall, but memory several times over and the histories still lost between them.
- **FSR 3.1 now**: the engine's own upscaler is measured first (ADR-0048).
- **Sharpening the history**: each frame sharpens what the last one sharpened, until it rings.
- **Dynamic resolution on the GPU**: no latency, but the rules could not be tested on the CPU, and the frame's sizes are chosen when it is recorded.
