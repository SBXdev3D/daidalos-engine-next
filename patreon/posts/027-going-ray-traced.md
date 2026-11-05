---
title: "Going ray-traced"
type: roadmap
status: draft, 1 passage for me to write
covers: the decision to move rendering to hardware ray tracing, the new Phase 9 (5 November 2026)
images: []
---

# Going ray-traced

The factory has real lights now: shadows, ambient occlusion, particles. But every one of those effects is an approximation. Only the four lamps nearest to the player cast shadows. Light never bounces off a wall onto the floor next to it. The shine on a metal pipe reflects nothing in the room. So I have decided where the renderer goes next: hardware ray tracing, for everything that light does.

[TO WRITE: why I want ray tracing in my engine, and what it means after starting with OpenGL without shaders in 2008.]

## Why indoor scenes need it

Indoors, most of what you see is light that has bounced at least once. There are many lamps, glossy floors and metal everywhere, and thin walls between a lit room and a dark one. The classic tricks handle each of these poorly: shadow maps are expensive per light, screen-space effects only know what's on screen, and light probes leak through walls. Rays follow light where it really goes.

## The new Phase 9

It comes after the menus and developer tools of Phase 8, in twelve small steps. Each one replaces an old technique and deletes it, and the demo stays playable after every step:

1. A newer Vulkan, ray tracing required, and a new shader language, Slang.
2. A render graph that works out the GPU synchronization by itself.
3. Acceleration structures: the scene in a form rays can search quickly.
4. A **reference path tracer**: slow, exact, and used to check every real-time technique.
5. Ray-traced shadows for every light, not just four.
6. Motion vectors and temporal antialiasing.
7. A denoiser, because real-time ray tracing gives a noisy image.
8. Bounced light, through a cache of light stored in the world.
9. Ray-traced reflections and glass.
10. ReSTIR, which lets thousands of lights share a few rays per pixel.
11. Upscaling, bloom, auto-exposure and HDR output.
12. Volumetric fog with ray-traced shadows: light shafts in the dust.

## No fallback

Ray tracing is mandatory: the engine will need an RTX 30xx class card or equivalent, and won't run on older ones. Keeping the old techniques as options would mean writing and testing everything twice.

## My own denoiser

NVIDIA publishes excellent tools for exactly this, its denoiser among them. But their licence forbids using them in a way that makes them subject to a licence that allows derivative works, and the engine is shared under CC BY-NC-SA. So the engine gets its own denoiser, light cache and light sampling, written from the published research papers. They will start out behind NVIDIA's, and I'd rather have that than a licence problem.

## What waits

Some techniques in the plan wait until measurements ask for them: meshlets, mesh shaders, a visibility buffer, portals. Today, drawing the factory's meshes costs well under a millisecond; the rays will be the expensive part. Neural rendering is out for now: my graphics card can't run it.

## The target

60 frames per second at 1080p on my RTX 3090, the only card I can test on.

## Next

First, Phase 8.2: menus, a pause screen and options that save.
