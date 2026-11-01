---
title: "The factory finds its voice"
type: progress
status: draft, 1 passage for me to write
covers: Phase 6.2 (1 November 2026)
images: []
---

# The factory finds its voice

*No picture in this post: this one is for the ears.*

The rebuilt factory has been silent until now. Today it has footsteps, doors, pickups, alarms, fans and its old music, all played by a new `audio` module.

[TO WRITE: how it felt to walk through the factory and hear it again.]

## What you hear

- **Footsteps** come from the distance walked, not from a timer, so they follow your speed. There are four of them, and the same one never plays twice in a row.
- **Doors, alarms and fans** have a place in the world. They get quieter with distance and move between your ears as you turn.
- **Music** is read from the disk while it plays, instead of being decoded into memory. It's the old `enter_the_maze` track.

That track was a 31.4 MB WAV file, too heavy for the repository. It's now a 3.4 MB OGG file, with the same 178 seconds.

## A library, and a small layer

I use **miniaudio**, an open-source library that handles sound devices, mixing and 3D placement. Like Jolt, it stays hidden inside one module.

Ownership follows the same split as the rest of the engine. The application owns the sound output, and each scene owns its sounds, so restarting a level also restarts what you hear. Looping sounds only start once the new scene really runs, because otherwise a restart would play two alarms during the swap.

## Testing something you can't see

The automated build machines have no sound card, so the engine can also mix without one and hand the samples to a test. The tests then check real audio: is a sound on the right louder in the right ear? Does it swap when you turn around? Does it fade with distance?

A machine with no output simply runs the game silently, with a warning in the log.

The memory checker also earned its keep. Feeding miniaudio a corrupt file made it read memory it had already freed, inside the library itself. The engine now checks each file with a plain decoder first, so a broken sound is a clean error that names the file.

I also read the Windows output meter while the game ran. It went from 0 before the launch to a peak of 0.51 during play, so sound really reaches the device.

## The numbers

| | |
|---|---|
| Automated tests | 426, up from 408 |
| New module | `audio`, the fourteenth |
| Sounds in the level | 19, loaded in 15 ms |
| Sounds at once | 64 at most |

## What isn't done

- **Sound goes through walls.** Nothing blocks it yet, so an alarm behind a closed door sounds the same.
- **The balance is untested by ear.** The tests and the meter prove that sound comes out, not that the mix sounds good. Volumes will need tuning.
- **Volumes are read at startup.** Changing them while playing needs the settings menu of Phase 8.
- **No zombies or weapons yet**, so their groans and gunshots are still waiting.

## Next

Phase 6.3: skeletal animation, so that the zombies can finally move.
