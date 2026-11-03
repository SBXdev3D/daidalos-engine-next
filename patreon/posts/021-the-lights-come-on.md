---
title: "The lights come on"
type: progress
status: draft, 1 passage for me to write
covers: Phase 7.1 (3 November 2026)
images: []
---

# The lights come on

Until now, the factory's darkness was a trick: fog that swallowed everything a few meters away, and a cone cut through it for the flashlight. Now the darkness is real. It's simply the absence of light, and the factory's own lamps light it again.

[TO WRITE: what it felt like to walk into the first lit room.]

*Still no screenshot: the diffuse textures are free assets whose licences I have to find first.*

## Every lamp of the old level

The old level had 8 ceiling lamps, two of them switched off, and two alarms. They're all back, with the values the old engine gave them: their color, their range and the width of their cone. Each **alarm** turns two red spots back to back, 25 degrees below the horizon, at 60 degrees per second, as the old code did. The **flashlight** is now a real light that follows your eyes, and every **shot** lights the room for 60 milliseconds.

A light is now an ordinary thing in the world, like a door or a zombie: something placed and turned. So a lamp on a moving platform would need no extra code.

## My normal maps are back

I made normal maps for most of the old textures: the bricks, the floor tiles, the crates, the lockers, the zombie. A normal map tells the renderer which way each pixel faces, so that a flat wall shows the relief of its bricks when a light moves past it. The conversion script had been dropping them, and now 24 of the 33 materials use one.

Two details had to be right:

- **Which way is up in a normal map** differs between tools: green can mean "up" or "down" the image. Claude Code measured it on my maps rather than guessing: just above a dark mortar line, the surface slopes down into the groove, so the normal must lean down the image. All four maps it checked agree with the modern standard, so they're used as they are.
- **The tangents**, the direction along which a normal map is read, are now computed with MikkTSpace, the standard that modelling tools use when they bake normal maps. The old engine computed one per triangle, which can show seams.

## Modern shading, old numbers

Surfaces are now shaded the way current engines do it (physically based rendering): rough tiles spread the light, smooth metal reflects it. The image is computed with a wide range of brightness, then brought back to the screen's range, so a bright lamp doesn't simply turn everything it touches flat white.

What I kept is the old engine's way of describing a light. A grey lamp of 0.4 lights a white wall to 0.4, and the light fades to nothing at its range. The old level's numbers carry over directly, with no guessing in physical units.

## The numbers

| | |
|---|---|
| Lights in the level | 8 lamps, 4 alarm spots, the flashlight, the muzzle flash |
| Materials with a normal map | 24 of 33 |
| GPU time, factory, 1280×720 | 0.74 ms for the scene, 0.06 ms to bring it to the screen (RTX 3090, release build) |
| Automated tests | 464, up from 455 |

The renderer now measures its own passes on the graphics card, and the debug overlay shows them, so every change from here on is measured.

## What isn't done

- **Lights shine through walls**: there are no shadows yet. That's the next part.
- **Every pixel looks at every light.** With 14 lights that's cheap, but it won't scale; light clustering comes after shadows.
- **There are no reflections**: metals only reflect an estimate of the room's light.

## Next

Shadows: the flashlight's first, then the three lamps nearest to you.
