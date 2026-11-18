# ADR-0077: Photo mode and frame captures

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 (ADR-0041) lists a photo mode: "a free camera, the path tracer converging, physical camera and image controls, high-resolution and EXR capture. The game provides its menu and rules." The engine could not save a frame at all.

## Decision

- **Captures in the renderer:** `Renderer::RequestCapture` marks the next frame. The output pass draws it a second time into an RGBA8 sRGB target, without the interface layer and in SDR whatever the display. The pass also copies the image it was made from: the lit scene after the lens, or the path tracer's. That copy is linear, exposed as displayed, and taken before bloom and the display transform. Both are copied into readback buffers. `TakeCapture` returns them when the frame's slot comes back, once the GPU has finished. Half floats are widened on the CPU. Debug views other than the path tracer keep only the PNG.
- **Files:** `resources::EncodePng` (stb_image_write) and `resources::EncodeExr`, a small writer of our own: one part, scanlines, no compression, 32-bit float B, G and R channels, readable by every EXR reader. `Application::CaptureFrame(base, exr)` writes them on a worker. `TakeSavedCaptures` reports the saved files. **F12 saves a PNG in any application**, in `Screenshots` beside `user.cfg`.
- **Photo mode in the sandbox** (`PhotoMode`, key P or the right stick's click, bindable):
  - The game pauses and the weapons are hidden.
  - A camera entity of its own leaves the player's eyes. It moves with the movement controls, Jump and Crouch (or the triggers) to rise and sink, and Run to go faster. It turns with the mouse or the right stick.
  - The game's rules: the camera stays within 12 m of the eyes and stops 0.2 m short of walls (a ray along each move). The scene's systems are paused, so photo mode propagates transforms itself.
  - The panel sets the focal length (12 to 200 mm on a 35 mm frame, which gives the vertical field of view), the aperture (all sharp, or f/1.4 to f/16) and the focus distance. "Focus on the centre" casts a ray to set the focus.
  - It also sets tilt, exposure (±3 EV), film grain and vignette, and the path tracer, showing its paths per pixel as it converges.
  - "Take the photo" saves the PNG and the EXR. H hides the panel and Tab frees the mouse.
  - The render settings photo mode changes (full resolution, exposure, grain, vignette, debug view) are set without being saved, and are put back when it closes: a setting that had its default is reset.

Tested: a PNG decodes back to its pixels, and an EXR has its header, offset table and scanlines of B, G and R floats where the format puts them (`ImageFileTest`). Seen in the factory at 1920x1080: the panel, f/2.8 with focus on the centre, the PNG saved without the interface, the 24.9 MB EXR, and the path tracer at 112 paths per pixel after 10 s. The readback itself has no automated test yet.

## Consequences

- **High-resolution capture (larger than the window) is not done.** The renderer's targets follow the swapchain's size. A capture size of its own would need the targets, the upscaler and the interface to be decoupled from the window, or tiled rendering, which screen-space effects (bloom, exposure, lens) do not survive. Captures are at the window's resolution: 4K on a 4K display.
- The path tracer has no lens: depth of field and the film's grain apply to the rasterized image only.
- The EXR is 12 bytes a pixel, about 44 MB at 2560x1440, because it is uncompressed.

## Alternatives considered

- **tinyexr or OpenEXR:** a dependency for what is about a hundred lines when uncompressed.
- **Reading the swapchain image:** it carries the interface and, on an HDR display, HDR10 values that a PNG cannot hold.
