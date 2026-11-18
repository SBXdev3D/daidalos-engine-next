# ADR-0070: Perception and utility AI

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 13 (ADR-0041) asks for perception (how lit the player is, from the radiance cache, and how much noise they make, from the sound system) and behaviour trees or utility AI for stalking, searching, hiding and ambushing, with the AI's state kept by the game. The factory's zombies saw as far in the dark as in the light, heard any shot within 20 m through any wall, and chased or waited.

## Decision

- **Perception is the engine's** (`scene/Perception.hpp`), and what AI makes of it is the game's.
  - **Noise:** the game makes noises (`Scene::MakeNoise`: where, how far it carries in meters walked, who made it), which the scene keeps a quarter of a second, so that AI thinking on fixed steps does not miss them. `Hears` says whether a listener hears one: when the way to it along the navigation mesh (ADR-0069), around walls and through open doors, is within its reach.
  - **Light:** `Illuminance` adds the lux the world's lights send to a point as they shine now, each shadowed by one ray against the physics world, and `Exposure` turns lux into how visible that makes something, from 0 to 1 (at 100 lux). It is not read from the GPU's radiance cache, as the roadmap first said: that would be a readback a frame or two late and a link from gameplay to the renderer, for light the AI needs only roughly; the estimate leaves out bounced light, profiles and cookies.
  - **Choice:** `UtilityChooser` picks the highest of a set of scores, keeping a bonus for the action it already does, so that close scores do not flip it every frame.
- **The factory's zombies** score seven behaviours each step: idle (waiting, patrolling), investigating a noise, searching around where they lost the player, stalking (seen from afar and watched, they hold still, and creep closer while the player looks away), chasing, ambushing (one in three waits at the last corner on the way to a noise, a doorway or a turn), and hiding (badly hurt, they back away). They see as far as 30% of their sight in the dark and all of it in light or under the player's flashlight. The player's steps carry 6 m (1.5 m crouching), shots 30 m, around walls.

Tested: a point light's lux fall with the square of the distance, a spot lights only its cone, a dimmed light gives its share and a skipped one nothing, exposure rises with light; a step just behind a wall is not heard by the walk around it while a shot is; the chooser keeps its action until another is clearly better (`PerceptionTest`).

## Consequences

- The AI is the sandbox's: another game writes its own behaviours from the same senses.
- Light estimates miss bounced light: a player in a corner lit only by a bounce counts as dark.
- Hearing costs one path query per zombie per noise: fine for the factory's dozen zombies, not for crowds.

## Alternatives considered

- **Behaviour trees:** clearer for scripted sequences of actions, but stalking, hiding and ambushing are choices among several pulls at once, which scores weigh more simply.
- **The radiance cache, read back:** exact light, a frame or two late, through the renderer.
