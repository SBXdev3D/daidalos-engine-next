---
title: "A new shader language, and nothing to see"
type: progress
status: draft, 1 passage for me to write
covers: Phase 9.1, Vulkan 1.4, Slang, ray tracing required (7 November 2026)
images: []
---

# A new shader language, and nothing to see

Phase 9 moves the engine to ray-traced lighting, in twelve steps. The first one is meant to change nothing on screen. It changes the language every shader is written in, the version of Vulkan the engine asks for, and the graphics cards it accepts. The test is that the game looks exactly as it did before.

[TO WRITE: how it feels to start the ray tracing phase with a step that shows nothing.]

## From HLSL to Slang

Since Phase 3, the shaders were written in HLSL, Microsoft's shading language, and compiled for Vulkan by DXC. They are now compiled by Slang, a newer language that started as a research project and is now hosted by Khronos, the group behind Vulkan. Slang has modules and generics. That will matter in step 4, when one description of how a surface reflects light must serve three different renderers.

For now, the shaders hardly change: Slang reads most HLSL as it is. The files got new names, and two lines changed because Slang refuses to turn a decimal number into an integer silently. Every warning is now an error, so the next one can't slip through.

## The one that wasn't a warning

One difference wouldn't have shown up as an error. When a mesh is drawn, each vertex knows its number. In Direct3D that number doesn't count the vertices before the mesh in a shared buffer; in Vulkan it does. DXC kept Vulkan's meaning, and Slang keeps Direct3D's. Animated characters read their skin weights by that number, so with the wrong one they could have taken another mesh's weights.

The Vulkan validation layers caught it indirectly: Slang asked for a feature the engine doesn't enable, just to do that subtraction. The shaders now ask Slang for Vulkan's numbering explicitly.

## Checking that nothing changed

Before switching, Claude Code compiled all 29 shader entry points with both compilers and compared what the graphics card sees: every binding, every field's offset, every matrix layout, every input and output. They were all identical, except for the vertex numbering above.

Then the game itself: screenshots of the same rooms from the same spots, before and after. Where nothing moves, they are identical to the pixel. The toxic fog differs, but two runs of the new version differ just as much from each other, because particles are random.

## Vulkan 1.4, and ray tracing required

The engine now asks for Vulkan 1.4 and refuses to start on a graphics card without hardware ray tracing. Nothing traces a ray yet; this step only makes sure the card can, so that the later steps can rely on it. On a card that can't, a window says what is missing instead of the game quietly closing.

The plan from post 027 listed two ways of tracing rays. I'm keeping only one for now, ray queries: a shader can trace a ray anywhere it likes, in the middle of shading a pixel or in a compute job. The other, dedicated ray tracing pipelines, needs its own machinery in the engine, and nothing needs it yet. The reference renderer of step 4 will use ray queries too: it must be correct, not fast.

## The numbers

Graphics card time per frame, at 1280×720 on an RTX 3090, in a release build, averaged over about 30,000 frames, two runs each:

| Pass | DXC | Slang |
|---|---|---|
| Meshes | 0.060 ms | 0.051 ms |
| Particle simulation | 0.091 ms | 0.091 ms |
| Shadows | 0.046 ms | 0.073 ms |

Particles were 15% slower at first, until Slang was asked to optimize harder. The shadow pass looked slower, but a later run of the very same build measured it at 0.045 ms: it is the first pass of the frame, and its timing wobbles between builds (more on that in the next post).

| | |
|---|---|
| Automated tests | 557, all passing |

## What isn't done

- **The Linux build hasn't run yet.** GitHub's automated builds are paused on my account, and the software graphics driver they test with needs a recent version to offer ray tracing. The build is set up for it, but it hasn't been tried.
- **One test fails under the memory checker**, inside the audio library, when it questions this PC's sound card. It isn't related to this step.

## Next

Step 2: a render graph, so that the engine works out by itself in what order passes run and when the graphics card must wait between them.
