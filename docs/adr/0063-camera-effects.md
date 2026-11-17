# ADR-0063: Camera effects

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 8 (ADR-0041) asks for film grain, a vignette, lens dirt lit by the flashlight, optional chromatic aberration and motion blur, and depth of field from the camera's aperture. The image so far was clean as a render: the horror look needs the imperfections of a camera.

## Decision

- **The lens and the film are in the output pass** (`Output.slang`), on the lit scene only (not debug views or the path tracer's reference), each from 0 to 1 in `ImageConfig` and a `renderer.*` setting:
  - **chromatic aberration** (`renderer.chromatic_aberration`, off): red and blue are read a little apart towards the edges;
  - **lens dirt** (`renderer.lens_dirt`, 0.5): smudges and specks on the lens, procedural (blobs in a coarse and a fine grid, no texture to license), that scatter the bloom: bright light in the view lights them, as the flashlight on a near wall or a lamp in the dark does;
  - **vignette** (`renderer.vignette`, 0.5): light falls off towards the corners about as the fourth power of the cosine, before tone mapping, so it darkens as light does;
  - **film grain** (`renderer.film_grain`, 0.35): new each frame, after tone mapping, strongest in the mid-tones.
- **Depth of field and motion blur are a compute pass** (`CameraEffects`, group "Camera effects"), after the temporal resolve and before bloom and metering, at the output's size, reading the G-buffer's depth and motion vectors in the corner they are drawn into:
  - **depth of field** from the camera (`CameraView::aperture`, an f-number, 0 for none, and `focusDistance`), by the thin lens on a 24 mm high sensor with the frame's field of view: a gather of 32 samples on a disc as wide as the pixel's blur, each counting where its own blur reaches the pixel, so a sharp point in front does not smear onto the blur behind it; at most 24 pixels;
  - **motion blur** (`renderer.motion_blur`, the fraction of the frame the shutter is open, off): 12 samples along the pixel's motion, at most 32 pixels.
- `scene::Camera` carries the aperture and focus; the sandbox sets them with `sandbox.aperture` and `sandbox.focus`, for screenshots now and photo mode later (Phase 15).

## Consequences

- The near field of depth of field does not spread over what is behind it: a blurred object in front keeps a hard silhouette. A separate near-field pass would fix it, if photo mode needs it.
- Motion blur follows the camera's motion too, which is why it is off by default in a first-person game.
- Grain and dirt are fixed by design, not by a texture: an artist's dirt image would need a texture input.

## Alternatives considered

- **A lens dirt texture:** more control, but an asset to make or license; the procedural one costs a few instructions per pixel.
- **Depth of field before the resolve, at the render resolution:** cheaper, but the temporal history would smear the blur as the focus changes.
