---
title: "Under the hood, while it runs"
type: progress
status: draft, 1 passage for me to write
covers: Phase 8.3, developer tools (7 November 2026)
images: []
---

# Under the hood, while it runs

The last step of Phase 8 isn't for players. In development builds, the key under Escape now opens the engine's own tools over the running game: a console, live statistics, every entity of the level with its components, and a list of what is loaded.

[TO WRITE: how I debugged the original Daidalos without tools like these, or what I missed most.]

*Still no screenshot, for the same licence reason as before.*

## A console

The console shows the engine's log as it happens, and takes commands. Type a setting's name to see it, or its name and a value to change it on the spot: `view.fov 70`, and the view widens at once. Tab completes names, the arrows bring back earlier lines. The game adds its own commands: in the demo, `restart` starts the level over.

A change made in the console lasts until the game quits. Settings that stick belong to the options menu from the last post, so an experiment never ends up saved by accident.

This needed one thing the engine didn't have: text. Until now it only knew which keys went down, not which letters they made. That depends on the keyboard layout, accents and dead keys, so the operating system works it out, and the engine now passes its text along. The key that opens the tools is chosen by its position, not its letter, so it's the same key under Escape on a French keyboard and an American one.

## Statistics

One line stays in the corner while playing: frames per second, frame time, GPU time, draws and triangles. Open the tools and you get the details: a graph of the last 240 frames, the time of each pass on the graphics card, how many lights cast a shadow, and how much video memory the engine uses. The demo uses about 154 MB.

## Looking inside entities

Since Phase 2, everything in a level has been an entity with components: a position, a mesh, a light. Now I can look at them while the game runs: the level's tree on the left, the picked entity's components on the right, and most of them can be edited while the game runs, such as a light's color and range or an object's position.

C++ can't list a type's fields at runtime by itself, so each kind of component gets a few lines that draw it. The engine provides those for its own components, and the game adds its own: the demo's player shows health, armor, weapons and ammunition, all editable. It's the quickest way to test a pickup or a death.

## A bug in the pictures

The first screenshots showed white specks inside one window, and only there. They turned out not to be a bug in the tools: they were bright pixels of the scene, showing through. The engine mixes the overlay with the scene in linear light, which is right for colors, but it makes a window that is 94% opaque let through what looks like a quarter of the brightness behind it. The tools' windows are now fully opaque.

## How it's tested

The console's language is tested without any window: setting and showing values, mistakes that change nothing, arguments in quotes, completion, history, and a log written from four threads at once. Other tests check the text event, the list of entities, what an asset library says it holds, and that the renderer counts exactly what it drew. A last one opens every tool window over a real scene, runs commands and picks an entity, with the graphics card's validation watching.

Then Claude Code opened the tools in the running demo, typed commands, and checked each window on screenshots. That's how the white specks were found.

## The numbers

| | |
|---|---|
| Tools on the graphics card, every window open | 0.04 to 0.08 ms |
| Automated tests | 557, up from 542 |

At 1280×720 on an RTX 3090, in a release build. Builds without the developer tools don't contain them at all.

## What isn't done

- **Entities can't be created or destroyed from the tools,** because their physics bodies and sounds would be left behind.
- **The CPU cost of the tools wasn't measured.**

## Next

Phase 8 is done. Next comes Phase 9, the move to ray-traced rendering.
