# ADR-0121: Light that follows a turn

- **Status:** Accepted
- **Date:** 2026-11-22
- **Amends:** ADR-0039 (the diffuse history is also kept near the short histories around it), ADR-0045 (more rays where a surface just came into view)

## Context

The owner still saw artifacts on lit walls in low light and when the camera moves fast, and asked whether they came from the denoiser, the radiance cache or the rays.

Screenshots of the factory taken while the camera turned were too noisy a measure: frame timing moves the view between runs, and texture detail drowns the light's noise. The dark corridor (ADR-0045) answered instead, with a turn the player makes, 30 degrees over 10 frames with the flashlight, scored against the path tracer.

- **The grain is indirect light's one ray.** Standing still, the corridor's flicker is 0.012 of its brightness without indirect light and 0.75 with it; reflections and fog change nothing. The denoiser takes the raw ray's noise from 7.5 to 0.75; four rays halve what is left.
- **After a turn, old light lingers.** The far end of the corridor, which the beam lit before the turn, is dark in the path tracer's view, and the engine's stays bright for 30 frames and more. Without the denoiser, the same pixels are right within a few frames: the lingering is the denoiser's history. Those pixels are no longer lit by the flashlight, so the cap of ADR-0051 never applies to them. The temporal pass keeps the history within the range of the light around the pixel, widened by the noise of one frame (ADR-0039); that noise is the pixel's spread over its frames, and the bright frames themselves make it as large as the old light. The range lets the history stay, and it fades at 1/32 a frame.
- **Surfaces that come into view** start from one ray, which the denoiser can only blur.
- **The radiance cache also lags:** right after the view turns 30 degrees in one frame, the raw bounce light is 40% too dark and recovers over about 16 frames. The denoiser's lingering hid it, by chance, in the corridor's lag score.

## Decision

### The diffuse history near its short history

The temporal pass keeps the diffuse history near the short histories around it before the neighbourhood's range bounds it (ReLAX's fast history, Zhdan 2021, without its textures). The moments' luminance, which the denoiser already keeps, is a short history of about five frames (`kMomentsWeight`). The long history's luminance is kept within

- the mean of last frame's short histories over the 5 × 5 pixels on the same surface around where the pixel was,
- give or take one standard deviation: the noise of that mean (the short histories' spread over the number of pixels) and the light's own detail (the long histories' spread across the same pixels: a shadow's edge, a gradient), plus 2% of the mean.

Measured across pixels in one frame, that bound does not grow when the light changes. The mean of 25 pixels, unlike one pixel's few frames, is not pulled by a rare bright sample, which a tighter bound would throw away, darkening the light. The neighbourhood's range comes after and wins, so a light that goes out still shows at once. Specular light keeps only the range: a reflection moves with what it reflects, not with the pixels around it.

Each group of 8 × 8 pixels reads last frame's around where its centre was into a tile of 16 × 16 in shared memory, and each pixel its 25 neighbours from the tile; a pixel whose motion takes them outside the tile, on a moving object, reads them itself. Reading them directly, the denoiser took 0.4 ms more at 1080p.

### More rays where a surface just came into view

`renderer.gi_new_rays`, from 1 to 16: the rays of indirect light of a pixel the denoiser has no history of, on its first frame (`Denoiser::PreviousGuides`, found through the motion vectors as the denoiser finds it). **4 on Ultra, 1 elsewhere**, where the test is skipped. The count is the top byte of `GiPushConstants::counts`.

### The cap where a light moved stays

Removing the cap of ADR-0051, and keeping the history within a short one instead, was built and measured: a swaying flashlight's flicker fell from 0.0186 to 0.0079, but during a turn the beam's light followed later, and the turn scored worse (0.642 against 0.589). The cap is what lets the beam's light keep up while the camera moves, which is when the flashlight moves.

## Measurements

Dark corridor, 160 × 90, against the path tracer; RTX 3090.

| | Before | After |
|---|---|---|
| Still: FLIP of the 32nd frame / flicker | 0.111 / 0.0078 | 0.105 / 0.0080 |
| Light left behind on pixels that should be dark, 10 frames after a turn stops, as a share of what was too much when it stopped | 0.48 | 0.355 |
| FLIP as the turn stops / 10 frames later | 0.582 / 0.434 | 0.593 / 0.397 |
| Bounce-lit pixels within 10% after a turn in one frame | 0 frames | 0 frames |
| The wall a beam left, direct light alone, under a tenth | 0 frames | 0 frames |

With 4 new rays, FLIP as the turn stops is 0.585 against 0.600 with one, and the same ten frames later.

GPU time at 1920 × 1080:

- **Denoiser**, factory start room on Ultra, three runs each: 1.26 to 1.28 ms before, 1.47 to 1.54 ms after: **+0.24 ms**.
- **Indirect light**, dark corridor, the view still or turning back and forth by 3 degrees a frame (`DISABLED_TimeNewRaysWhileTurningAt1080p`): 0.77 ms with one new ray while turning, 0.89 ms with two, 1.10 ms with four (+43%); still, no change. With the test at under 4 frames instead of the first, four new rays took 1.28 ms while turning for little more gain: a fifth of the pixels counted as new.

## Consequences

- Old light fades within about ten frames after a turn rather than thirty; the frame the camera stops on is about as far from the path tracer as before.
- The denoiser costs 0.24 ms more at 1080p. Ultra traces up to 40% more indirect light while the camera turns.
- **The radiance cache's lag is left.** It now shows where the lingering hid it: bounce light a little dark for a dozen frames after a fast turn. It is the next thing to look at, in the cache.
- One-ray indirect light remains the grain of dark walls standing still; more rays everywhere (`renderer.gi_rays`) remain the way to buy it down.

## Alternatives considered

Each built and measured in the dark corridor:

- **No cap where a light moved**, see above.
- **Restarting the history where the light changed**, judged by the pixel's neighbours against its history: noise tripped it, and after a turn the denoiser followed the cache's dip down and stayed dark for 40 frames; a rare bright sample in a short history passed for a change and was thrown away, darkening noisy light on average.
- **A bound around the pixel's own short history**, its noise from its spread over its frames: the change itself widens it. From the spread across pixels, centred on the pixel: the still corridor's flicker rose by 60%. Centred on the pixel or its neighbours' mean, whichever is nearer: no gain.
- **Widening the neighbourhood's range by at most the short histories' spread**, instead of a bound of its own: the still corridor's FLIP rose from 0.111 to 0.162, the light darkened.
- **A separate pass after accumulation:** it undid the neighbourhood's range, so a light switched off stayed lit.
- **9 taps two pixels apart** instead of 25: a third of the reads, and most of the gain gone (0.45 left behind).
- **A faster short history** (`kMomentsWeight` 0.25 or 0.33): less left behind (0.32 at 0.25, 0.26 at 0.33), and a still corridor that flickers 12% and 28% more, the moments also giving the spatial filter its variance. Low light's noise was the first complaint.
- **More rays below 4 frames of history**, see above.
- **A fast history in textures of its own, from a pre-blurred frame**, as ReLAX keeps it: more passes and memory, for a lag the short history in the moments already brings to ten frames.
