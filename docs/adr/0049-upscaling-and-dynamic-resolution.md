# ADR-0049: Temporal upscaling and dynamic resolution

- **Status:** Accepted, amended by 0050
- **Date:** 2026-11-14
- **Amends:** ADR-0037 (the resolve becomes an upscaler; sharpening is the engine's own), ADR-0048 (the test that decides whether FSR 3.1 is added), ADR-0041 (FSR 3.1 becomes step 9.16; volumetrics 9.17, presets and pipelines 9.18)

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
- **`FrameData` gains** the render size, last frame's render size, the targets' size and the mip bias; it grows from 400 to 432 bytes. `screenTexel` is a pixel of the corner, so texture coordinates of the frame still go from 0 to 1. Loads by pixel are unchanged. Filtered reads go through `FrameToTarget`, which scales their coordinates to the targets' and keeps them half a texel inside the drawn corner, so that they never read what an earlier, larger frame left outside it. ReSTIR DI's reservoirs are laid out over the whole targets, so that a pixel keeps its index when the size changes. Compute passes dispatch over the render size, which their inputs give (`extent`); `rhi::RenderingDesc::area` limits a rendering, its clears included, to the corner. The projection's aspect is the output's, so that rounding the render size to whole pixels does not change the view.
- **Debug views and the path tracer render at 100%**, as they skip the resolve.

### The upscaler

- **`TemporalResolvePass` becomes the upscaler**, one compute pass at output resolution, for every scale; at 100% it is the temporal antialiasing it was. Its inputs are those FSR takes (color, depth, motion, reactive mask, jitter, the two sizes), so that FSR could replace it.
- **Per output pixel:** the render pixel whose jittered sample landed nearest to its center, weighted by a Gaussian of that distance (σ half an output pixel), so that a sample counts most where it landed. The history, at output resolution, keeps in alpha the weight of the samples it holds, and blends with the new one in proportion: a weighted running average, capped at 6.5, which at 100% is about the ten frames ADR-0037 held. The cap falls to 3 as the image moves by up to 2 output pixels a frame, as each resampling of the history blurs it a little. The neighbourhood box and the nearest depth's motion come from the 3 × 3 render pixels, as before. Where the history starts over below 100%, the frame is filtered at each output pixel by a Gaussian of its samples' distance, which shows no blocks.
- **The jitter's cycle grows with the scale**: Halton (2, 3) over 8 / scale² points, 18 at 67%, so that each output pixel is covered.
- **Textures are sampled with a mip bias of log2(scale)** in the G-buffer, so that they keep the output's detail. The RHI's samplers have no bias; the G-buffer's one material read takes it from `FrameData`.
- **Sharpening**, which ADR-0037 left to FSR: a contrast-adaptive sharpening as FSR's RCAS does it (five taps, limited by the neighbourhood's contrast so that it does not ring), applied in the output pass, so that the history is never sharpened. `renderer.sharpness`, from 0 to 1: 0.25, chosen from screenshots of the factory at 1440p. Only a resolved frame is sharpened.

### Dynamic resolution

- **On the CPU**, as auto-exposure is (ADR-0048), and tested there: the GPU time of the frame that comes back, `kFramesInFlight` frames later, from the timestamps the statistics already read.
- **Down at once, up slowly**: over the budget, the scale drops to what the budget allows, assuming the cost follows the pixels; under 85% of it for half a second, it rises by a step. Steps of 2.5% of the scale, so that it does not hunt.
- **Settings:** `renderer.render_scale` (1.0 by default): the scale when dynamic resolution is off, its ceiling when it is on. `renderer.dynamic_resolution` (on). `renderer.min_render_scale` (0.5). `renderer.target_fps` (30 to 240; 60): the GPU's budget is 90% of its frame time. The menu has a Resolution row: Dynamic (the default: up to the window's, down to half as the GPU needs), or fixed at Native, Quality (67%), Balanced (58%) or Performance (50%). A Target frame rate row sets the frame rate dynamic resolution holds, from 30 to 240 fps (60 by default); the options' rows are 50 pixels high instead of 54 to fit it. The statistics show the render size. Without temporal antialiasing, and in debug views, the scale is 1.

### Whether FSR 3.1 is added

ADR-0048's test is replaced. **Reference**: the same view rendered at twice the width and height with temporal antialiasing, averaged down to the output's size. Native temporal antialiasing and the upscaler at 67% are compared with it by FLIP, still and along a camera pan, and by flicker. **FSR 3.1 is added, in a step of its own, only if the upscaler's FLIP exceeds 1.15 times native resolution's, or screenshots show ghosting or shimmer that native resolution does not.**

The factory cannot be loaded by the GPU tests; the scenes built in code stand in for it: the dark corridor, the mirror room with its glass, and the corridor's parts glowing bright and dim in turn with nothing lit, edges alone, where only the upscaler differs from one scale to the next. The factory is checked by screenshots.

## Measurements

RTX 3090, 320 × 180 output, the last eight of 48 still frames, then 32 frames of a pan of half a degree a frame (`UpscalingTest.DISABLED_MeasureEachScale`):

| FLIP, still / panning | 100% | 67% | 58% | 50% |
|---|---|---|---|---|
| Dark corridor | 0.093 / 0.091 | 0.105 / 0.101 | 0.113 / 0.108 | 0.117 / 0.115 |
| Mirror room | 0.063 / 0.058 | 0.085 / 0.083 | 0.093 / 0.088 | 0.104 / 0.096 |
| Edges alone | 0.0028 / 0.0053 | 0.0044 / 0.0065 | 0.0056 / 0.0073 | 0.0061 / 0.0078 |

At 67%, the dark corridor is within the rule (1.13 and 1.11 times native); **the mirror room is not (1.34 and 1.43 times): by the rule, FSR 3.1 is added, as step 9.16**, measured by the same test, and kept only if it beats the engine's own. Much of the mirror room's gap is not the upscaler's: its reflections, and the denoiser that works in render pixels, are coarser below 100%, and FSR would get the same input. Edges alone, the upscaler is 1.6 times native still and 1.2 times panning, in errors twenty times smaller than the lit scenes'.

What was tried and dropped, measured the same way: every sample of the neighbourhood weighted by its distance, instead of the nearest one (no better, and worse lit); a narrower or wider weight (narrower aliases edges into dashes, wider dilates them); a longer history (no better); a jitter cycle of 64 positions (no change); the clipping box from the 2 × 2 render pixels around the output pixel (better on edges, worse in the mirror room). The shorter history in motion was kept: on edges alone, panning went from 0.0119 to 0.0065. A single frame's FLIP moves by 10% with the noise of the lights, so scores average eight frames.

A reference averaged from 64 jittered frames at twice the size, with no temporal antialiasing, ranks the scales alike: native's TAA is not flattered by a reference made the same way.

GPU time in the factory, from the developer tools, at the player's start:

| | 100% | 67% | 50% |
|---|---|---|---|
| 1920 × 1080 | 6.46 ms | 3.37 ms | |
| 2560 × 1440 | 11.48 ms | 6.60 ms | 3.99 ms |

The resolve costs 0.21 ms at 1080p and 0.32 ms at 1440p, at any scale; the passes at render resolution cost about the share of pixels they keep. At 1440p and 67% with sharpening, the factory's floor keeps the detail of native resolution in screenshots; without sharpening, it is softer.

## Tests

- **Unit:** the jitter's cycle per scale; the controller's descent at once, the frames it passes over after a change, its slow ascent, its hold near the budget, its bounds; the render size's rounding.
- **GPU:** the resolve's tests at 100% are unchanged; at 67%, an edge converges to what native resolution shows, a jumping square leaves no ghost, and below the drawn corner the old frame is never read while the size changes every frame; the denoiser's history outlives a change of size and is found where it was (read at the wrong place, the test fails); the renderer renders at fixed scales, a scale changing every frame and a budget no frame holds, with synchronization validation clean.
- **`UpscalingTest`**: the dark corridor and edges alone at 67% against native, within 1.2 times lit and 1.4 times panning on edges.

## Consequences

- Scales below 100% cost the passes at render resolution about the share of pixels they keep: at 67%, 45%.
- The targets at render resolution keep the output's memory at every scale.
- Every pass that turns pixels into texture coordinates uses the render size: a new pass that uses its target's size instead is wrong below 100%, and the tests at 67% are there to catch it.
- Dynamic resolution is checked with a budget no frame holds, in the renderer's test; on the RTX 3090 the factory holds 60 fps at 1440p native (11.5 ms), so it stays at 100% there.
- FSR 3.1 is step 9.16; volumetrics move to 9.17, presets and pipelines to 9.18. ADR-0050 built it: the mirror room's gap was the upscaler's, FSR did not beat the engine's by this measure but flickers less, and it is kept as an option.

## Alternatives considered

- **Reallocating the targets when the scale changes**: memory follows the scale, but each change is a stall and resets every history.
- **A few fixed resolutions, each allocated**: no stall, but memory several times over and the histories still lost between them.
- **FSR 3.1 now**: the engine's own upscaler is measured first (ADR-0048).
- **Sharpening the history**: each frame sharpens what the last one sharpened, until it rings.
- **Dynamic resolution on the GPU**: no latency, but the rules could not be tested on the CPU, and the frame's sizes are chosen when it is recorded.
