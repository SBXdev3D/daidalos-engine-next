---
title: "Light stops at the walls"
type: progress
status: draft, 1 passage for me to write
covers: Phase 7.2 (3 November 2026)
images: []
---

# Light stops at the walls

Last time the factory's lamps came back on, but their light went straight through walls and pillars. Now spot lights cast shadows: the alarms' red beams stop at the pillars, and the flashlight's shadows follow your hand.

[TO WRITE: a moment where a shadow changed how the factory feels.]

*Still no screenshot, for the same licence reason as before.*

## Four shadows at a time

Each shadow costs a picture of the scene seen from the light, every frame. So there's a budget: **the four lights nearest to you get a shadow**, among those that ask for one. The flashlight, being in your hand, always comes first. The other lamps still light their rooms, just without shadows, until you walk closer.

The four pictures share one big texture, cut into four squares. The old level's own flags say which lamps cast shadows, and the alarms do too, as they did in the old game.

## Shadows without the usual artefacts

Shadow maps have two classic flaws: surfaces that shadow themselves in stripes ("acne"), and shadows that float away from their objects. Claude Code fights both by nudging each lit point slightly towards the light, more so far from the lamp where each texel of the shadow covers more ground. Edges are softened by mixing nine samples.

Two things must never cast a shadow:

- **A lamp's own shade.** The light sits right at the bottom of it, so anything closer than 10 cm to a light is ignored.
- **The weapon in your hands.** It sits right in front of the flashlight and would darken half the room, so held weapons are marked as not casting shadows, as in most shooters.

## A false alarm

While checking the result, Claude Code thought it saw a dark crescent in a lamp's pool of light, and suspected the lamp's shade. It changed the code, and the crescent stayed. So it turned that lamp's shadow off entirely: the picture didn't change at all. There had never been a shadow there: it was just the edge of the light fading out, easier to see now that the rest of the floor isn't pitch black. It went back to the real question, and kept only the two fixes above, which were right anyway.

## The numbers

| | |
|---|---|
| Shadowed lights per frame | 4, nearest first |
| Shadow texture | 2048×2048, four squares of 1024×1024 |
| GPU time for the four shadows | 0.28 to 0.37 ms (alarm hall, 1280×720, RTX 3090, release build) |
| Automated tests | 468, up from 464 |

## What isn't done

- **Only spot lights cast shadows.** The level's lights are all spots, so the factory doesn't miss anything yet.
- **Every object is drawn into every shadow**, even far outside the light's cone. Culling comes later.
- **Every pixel still checks every light.** Light clustering is next.

## Next

A depth pre-pass, light clustering, and ambient occlusion: the darkening in corners and creases that makes a room look solid.
