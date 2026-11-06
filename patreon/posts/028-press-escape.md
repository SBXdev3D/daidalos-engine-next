---
title: "Press Escape"
type: progress
status: draft, 1 passage for me to write
covers: Phase 8.2, menus, pausing and settings (6 November 2026)
images: []
---

# Press Escape

Until now, the demo dropped you straight into the factory. Escape quit at once, and dying left a line of text telling you to press F5. Now the demo starts like a game: a loading screen, a main menu over the dark factory, a pause menu, options that are saved, and a proper ending, whether you die or clear the factory.

[TO WRITE: what menus and options meant for the original Daidalos, or how the old loading gear came to be.]

*Still no screenshot, for the same licence reason as before.*

## The old gear turns again

The original engine's loading screen said "LOADING..." in the middle of a black screen, with a red gear turning in the bottom-right corner. Both are back, with my old gear image, at the same size and the same speed: a quarter turn per second. There is no progress bar. The level is one file, loaded at once in the background, so a bar would have nothing true to show.

## Buttons drawn like everything else

The HUD from the last post is drawn again every frame, in order, with nothing kept between frames. The menus work the same way. Each frame, the game asks for a button, and the engine draws it and answers one question: did the player just press it? The game keeps its own values, like the volume or the field of view, and the engine only remembers which button has the focus.

So the code for a menu reads like the menu itself: draw the title, ask for "Resume", ask for "Restart", ask for "Options", ask for "Quit", and act on the one that was pressed.

There are four kinds of widget: buttons, on/off toggles, sliders and choices, like the antialiasing mode. The mouse works as you'd expect, and so does the keyboard: up and down move between them, Enter presses, left and right change a value.

## Stopping time

A pause menu has to stop the game, and not only the player. The zombies, the doors, the physics, the animations, the particles and every sound must hold where they are, and go on from there when you resume.

The engine can now do that for a whole scene with one call. Behind the menu, you still see the factory exactly as it was.

One detail took a test run to find: closing a menu with a click used to fire the gun, because the guns fire for as long as the button is held. The game now waits for you to release the button before it goes on.

## Settings that stick

The options cover what you'd expect to change while playing: fullscreen, vertical sync, antialiasing, ambient occlusion, brightness, field of view, the three volumes and mouse sensitivity. Each change applies at once: drag the music slider and you hear it.

When you leave the options, your changes are saved to a small file, `user.cfg`, next to the game. It's read after the engine's own settings, so yours win; and the command line still wins over both, for tests. Only what you changed in the menu is saved, so a setting given once on the command line doesn't stick by accident. The file is written in full under another name, then swapped in, so a crash can't leave half of it.

Another bug the test runs caught: opening the options and leaving them saved the mouse sensitivity, although nothing had changed. The slider rounded the value to its steps, and in floating point, 0.0025 rounded to the nearest step isn't exactly 0.0025. Now a slider only moves when you move it.

## Endings

Two seconds after you die, the game stops on "YOU ARE DEAD", with how many zombies you killed and a button to try again. Two seconds after the last zombie dies, it stops on "FACTORY CLEARED", with how long it took. The original demo had no ending at all.

## How it's tested

Automated tests feed made-up mouse and keyboard events to the widgets: a click on a button presses only that button, the arrows wrap around, a slider follows the pointer and stops at its ends, a slider left alone keeps its value, a click that changes screens doesn't press a button of the new screen. Others check that a paused scene runs nothing, that paused sounds hold their place and go on, that volumes change while playing, and that saving keeps your earlier settings but not the command line's.

Then Claude Code ran the real demo and clicked through it with a script: the menus, the options, a slider dragged to 110 degrees, Escape, a restart. That's how both bugs above were found.

## The numbers

| | |
|---|---|
| Menus on the graphics card | under 0.1 ms |
| Automated tests | 542, up from 524 |

At 1280×720 on an RTX 3090, in a release build.

## What isn't done

- **Menus don't work with a gamepad yet,** although the game itself does.
- **Held keys don't repeat** in menus: a slider moves one step per press.
- **Settings read at startup,** like shadow resolution, are left to the file.

## Next

Phase 8.3: the developer tools. A console, frame statistics, an entity inspector and a view of loaded resources, in development builds.
