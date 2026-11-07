---
title: "Who waits for whom"
type: progress
status: draft, 1 passage for me to write
covers: Phase 9.2, the render graph (7 November 2026)
images: []
---

# Who waits for whom

A graphics card doesn't run a frame one step at a time. It starts the next pass while the last one is still finishing, unless it's told to wait. Telling it when to wait is called a barrier, and until now the engine wrote every barrier by hand: about forty of them, spread across the renderer and its passes. Step 2 of the ray tracing phase hands that job to a render graph.

[TO WRITE: my experience with this kind of synchronization, from the Direct3D 9 days or since.]

## Declare, don't synchronize

Each pass now says what it uses: the textures it draws into, the ones it reads, the buffers it reads or writes. The depth pre-pass draws into depth and normals; the particle simulation reads depth and normals from a compute shader and rewrites the particle buffers; the tonemap reads the lighting and draws into the screen. The engine remembers, for every texture and buffer, what last wrote it and what has read it since, and works out the barriers from that.

The rule fits in a few lines. Writing waits for everything before. Reading waits for the last write, unless an earlier barrier already covered this kind of reader. And a texture that changes layout, the way the card arranges it in memory for one use or another, waits too. The rule is a plain function, so it has unit tests that don't need a graphics card.

The graph also names every pass for graphics debuggers, and times each group of passes. The developer tools from the last post now only list the passes that actually ran.

## What the checker found

Vulkan's validation layers have an optional mode, synchronization validation, that watches every read and write on the card and reports any pair that no barrier orders. It had never been switched on. Now every GPU test runs with it.

On its first run it found two problems, both older than the graph:

- **Multisampled depth.** When antialiasing uses multisampling, the depth is averaged down to one sample at the end of the pre-pass. Vulkan does that step at the same moment it writes colors, not when it tests depth, and the barriers around it waited for the wrong moment.
- **Two uploads into one buffer.** The engine batches its uploads to the card. Two uploads into the same buffer in one batch could overlap.

Reading the old code for the graph turned up a third: the particle simulation read the depth from a compute shader, but the barrier before it only covered the shaders that draw pixels. A heavy, catch-all barrier right after it happened to hide the mistake. None of these showed on screen on this card. That's what makes them dangerous: they're the kind of bug that appears on someone else's graphics card.

## The numbers

Graphics card time per frame, at 1280×720 on an RTX 3090, in a release build, averaged over about 30,000 frames, two runs each:

| | Before | After |
|---|---|---|
| Particle simulation | 0.091 ms | 0.053 ms |
| Whole frame | 0.31 ms | 0.31 ms |
| Automated tests | 557 | 573 |

The particle simulation got faster because it no longer stops the whole card between its steps: it waits only for the buffers each step uses.

Screenshots taken before and after match, to the same degree that two runs of the old version match each other.

## A correction

In the last post I wrote that the shadow pass got slower with the new shader language. Measuring for this step, the same build gave 0.045 ms, exactly what it was before. The shadow pass is the first one timed in a frame, and a timestamp is not a barrier, so its time depends on how much it overlaps the end of the previous frame. The difference wasn't Slang's.

## What isn't done

- **The graph doesn't reorder passes or share memory between them.** With a dozen passes there's nothing to gain yet. That may change when ray tracing brings bigger images.
- **Linux is still unverified,** for the same reason as last time.

## Next

Step 3: acceleration structures, the data the graphics card searches when it traces a ray.
