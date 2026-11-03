---
title: "Spring cleaning"
type: progress
status: draft, 1 passage for me to write
covers: audit between Phases 6 and 7 (3 November 2026)
images: []
---

# Spring cleaning

Before starting Phase 7 and its lights, I stopped adding features and asked Claude Code, my development assistant, to audit the whole engine: architecture, code quality, readability and performance. My main question was simple: is it easy for a human to read, and do we really need that many modules?

[TO WRITE: why I wanted a pause before Phase 7, and what bothered me when reading the code.]

Nothing in the game changed. Every change kept all the automated tests passing, on every step.

## Fifteen modules become nine

The engine was split into 15 small libraries for about 12,000 lines of code. Four of them were under 400 lines. Worse, the splits didn't always follow the code: the renderer, whose job is to draw, also created game entities and knew about animation.

The new rule is short: **a module exists only if it is a real layer, or if it hides a big third-party library.**

| Before | After |
|---|---|
| core, logging, math | **core** |
| platform, input | **platform** |
| resources | **resources** |
| rhi | **rhi** (Vulkan) |
| renderer, debugui | **renderer** |
| physics | **physics** (Jolt) |
| audio | **audio** (miniaudio) |
| ecs, animation, scene, and the renderer's entity code | **scene** |
| app | **app** |

The important change isn't the merging, it's the direction: **only the scene knows about game entities now**. The renderer draws a list of things to draw and nothing else. Physics knows bodies, audio knows voices, and the scene connects them to the entities. The dependency graph now fits in one sentence.

Code still reads the same way, with `math::Transform` and `ecs::World`: the names follow what things are, not which library they live in.

## Less code that says nothing

- **Passing an error on took four lines**, 66 times. A small `DAI_TRY` helper does it in one. Code that must undo something before failing keeps its explicit check, so those places stand out.
- **The startup sequence was a list of little functions** that started each part of the engine and remembered how to stop it. It's now straight code that reads top to bottom, with one function that stops everything in reverse order. Its class and its 9 tests are gone.
- **53 checks for "object used after being moved"** are gone: such a mistake already fails at once.

## Loading without waiting

The audit found one real performance problem. When a model was loaded, every piece of it sent to the graphics card was copied, then the engine **waited** for the card to finish, two or three times per mesh. The factory level has 58 meshes and 33 textures.

Now the copies are only written down, and they all go to the graphics card together, just before the next frame. Nothing waits for them.

| Main-thread time to build the factory level | |
|---|---|
| Before | 45 to 54 ms, about 3 frames at 60 Hz |
| After | 8 to 14 ms |

Measured in a release build, three runs each, on my machine. The rest of the load, reading and decoding the files, already happened on background threads.

Two smaller fixes: draws are now sorted so that the graphics card switches settings less often, and a model no longer copies the shape of a mesh once for each place that uses it.

## The numbers

| | Before | After |
|---|---|---|
| Engine modules | 15 | 9 |
| Test programs | 16 | 11 |
| Automated tests | 462 | 455 |
| Lines of engine, sandbox and test code | | about 370 fewer |

## What isn't done

- **Building a model's GPU objects still happens on the main thread.** About 10 ms is less than a frame at 60 Hz, but more than a frame at 144 Hz.
- **Draws are not culled or instanced yet**: those belong with the lighting work of Phase 7.
- **The Linux build is still unchecked**, as the automated build machines are paused.

## Next

Phase 7: real lights in place of the fog, shadows for the flashlight, and particles.
