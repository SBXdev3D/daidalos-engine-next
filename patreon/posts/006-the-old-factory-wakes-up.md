---
title: "The old factory wakes up"
type: progress
status: draft
covers: Phase 5 (1 November 2026)
images: []
credits: ""
---

# The old factory wakes up

The factory is the demo level of the old Daidalos Engine, the DirectX 9 engine I started in 2011 after two earlier versions of Daidalos: lamps and alarms throwing light through bars, crates you can jump on, a fan that turns.

[TO WRITE: what this old demo means to me, and how it felt to see it again.]

The rebuilt engine now runs a real level. Not a test scene: the factory from the demo I made with the old Daidalos engine, inspired by Doom and Quake. From now on, the sandbox is that demo's remake, and every phase of the roadmap has to make it better.

*No screenshot in this post yet. The geometry is mine and the textures and sounds are free ones, but I still need to find the site they came from so I can credit it.*

## An old level in a new engine

The old level is an XML file listing 1,097 tiles and props: floors, walls, ceilings, crates, railings. A small Python script now converts it to glTF, the standard format the engine already reads, merging tiles that share a texture.

Two details made this interesting:

- **The old engine was left-handed, the new one is right-handed.** Think of a world and its mirror image. Every point gets mirrored, and every triangle has to be flipped so that it still faces the right way.
- **Units became meters.** The old world used arbitrary units. Its camera stood 4.7 units above the floor, so one unit is now 35 cm.

It worked first time: 13,386 triangles, drawing at about 2,700 frames per second on my RTX 3090 with vertical sync off.

## What you can do in it

For now, you fly: collisions come next. But it's already more than a viewer. The numbers come straight from the old game:

- Doors open as you approach, stay open six seconds, and close again.
- Pickups spin in place, and flying through one collects it. Health goes from 80 to 100.
- The red door refuses to open until you find the red card.
- The level is dark, and that darkness is a trick for now: it's fog, not light. The flashlight is a single cone with no shadows. Real lights come in Phase 7.

## What the engine gained

- **Scenes.** A scene owns its world, its assets and its systems. When it goes, all of that goes with it.
- **Background loading.** A worker thread reads and decodes files while the window stays alive. Restarting the level loads a fresh copy in the background, which takes over once it's ready.
- **Input actions.** Game code now asks "is the player firing?" rather than "is the left button down, or the gamepad trigger?"

## A bug the old level found

Before the conversion handled every file, a level failed to load, and the engine crashed instead of reporting the error. The assets were being released after the renderer they depended on. It's fixed at the root: the application now owns the scene, and always releases it first. There's a test for it too.

## A change of plans

Physics and audio now come before the modern renderer. A game that you can walk through and hear gives every later feature a real test. Skeletal animation joins that phase too, because zombies need to walk, and shadows and particles have joined the rendering phase.

[TO WRITE: my own reason for wanting this order, if it differs.]

## What isn't verified yet

- **Linux.** This phase has only run on my Windows machine so far. The automated Linux build will check it.
- **Hitches.** Files load in the background, but creating the GPU objects still happens in one go at the end of a load.
- **The original.** I rebuilt the route through the level from its files, and I haven't yet replayed the old demo side by side.

## Next

Phase 6: walls that stop you, footsteps, gunshots, and something shuffling towards you in the dark.
