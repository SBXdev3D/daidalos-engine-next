# ADR-0123: Recording video

- **Status:** Accepted
- **Date:** 2026-11-23
- **Amends:** ADR-0077 (a capture per frame in flight, and frames that keep the interface), ADR-0122 (the tour is recorded by the engine)

## Context

The owner first chose to record the effects gallery's tour (ADR-0122) with OBS, then changed their mind: a screen recorder films what the frame costs, and a hitch, a dip below the display's rate or a frame that takes 30 ms shows in the video. They asked for the engine to export the video instead, as JPEG images rather than PNG, into a directory they choose.

The pieces were there. Photo mode reads a frame back (ADR-0077), but one at a time: a single capture in flight, so asked for every frame it returns one frame out of two. It leaves the interface out, and the tour's fades are drawn by the game's interface (ADR-0122). The audio engine can mix without a device, read on demand (ADR-0019), which tests use. The frame timer takes the time elapsed from the caller.

## Decision

- **`record.directory`** names where a recording goes; empty, there is none. The sandbox records its start sequence (`sandbox.sequence`) or its tour (`sandbox.tour`) when it is set, stops when the sequence ends, and quits once every frame and the sound are written. `record.fps` (60 by default), `record.format` (`jpg`, or `png`) and `record.quality` (95, for JPEG) are the rest. The frames are `frame_000000.jpg` onwards, at the window's size, and the sound is `sound.wav`, 48 kHz stereo 16-bit: `ffmpeg -framerate 60 -i frame_%06d.jpg -i sound.wav -c:v libx264 -crf 16 -pix_fmt yuv420p -c:a aac -b:a 256k tour.mp4` makes the video.
- **Each frame stands for 1/fps seconds**, however long it takes to draw: the application feeds the frame timer that step instead of the clock, the renderer's upscaler is told it (`Renderer::SetFixedFrameTime`) and dynamic resolution holds the render scale, and the scene's mix fades by it (`SoundWorld::SetFixedStep`). A recording is then the same whatever the machine, without hitches, at the quality settings in use, and may take longer than it lasts.
- **The sound is read, not played.** With `record.directory` set at the start, the audio engine has no device; each recorded frame reads the samples of the time it stands for, counted from the first frame so that rounding never drifts, after the frame's update has started its sounds. Nothing is heard while recording.
- **A capture per frame slot.** `FrameCapture` keeps a target and readback buffers per frame in flight, and a queue of the frames read back, so that asked for before each frame every frame comes back, a frame or two later and in order. A video frame keeps the interface layer (the fades, and subtitles if any) and leaves out the scene's light, which a photo's EXR needs and a video does not. A recording keeps its buffers from frame to frame; a photo gives them back.
- **Workers encode** (stb's JPEG and PNG writers), at most eight frames at once: beyond, drawing waits for them rather than holding frames in memory.
- **Quitting during a recording** keeps what was read back: the frames written and the sound up to them.

## Consequences

- `+record.directory D:/Video/tour` added to the tour's command line writes the tour as about 12 400 JPEG frames and its sound, then quits. At 2560x1440 a frame is 0.5 to 1 MB.
- The video is as sharp as the settings: `+renderer.quality ultra` or a full render scale costs time, not frames.
- Anything that reads the wall clock stays on it: the developer tools' timings, the sound's worker thread (its reverb and paths), shader reloading.
- A minimized window draws no frames, and a frame asked for and not drawn shifts the sound by a frame: the window stays open.

## Alternatives considered

- **Encoding the video in the engine** (FFmpeg's libraries, or a hardware encoder): a codec and its licence for what the ffmpeg command line does from the images.
- **Real time with a fixed frame rate**, the sound played and recorded from the device: a frame that misses its time would be a gap in the video, which is what OBS already gave.
- **Fades drawn by the renderer**, so that photos keep them: a photo is meant without the interface; the video frame takes the interface layer, which is one bound texture more.
