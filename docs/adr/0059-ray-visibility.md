# ADR-0059: Which rays see what

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 4 (ADR-0041) asks for ray visibility per entity: whether the camera, shadow, reflection and indirect rays see it, set in the editor and by the game's code. A horror game uses it for a figure seen only in a mirror, a shadow on a wall with nothing to cast it, a lamp's shade that should not darken the room it lights. The frame's acceleration structure already had instance masks: shadow casters, others, and glass (ADR-0035, ADR-0042), so a draw could only choose whether it cast shadows.

## Decision

- **One mask bit per kind of ray** (`RayTracingScene.hpp`, matched in `RayQueries.slangh`): shadow, camera, reflection, indirect, glass, and collision. Each pass queries with its own: shadow rays with shadow, the reflection pass and the glass pass's rays with reflection, the radiance cache and the bounce rays with indirect, particles with collision and glass. The path tracer's first ray uses camera; after a surface, a sharp one's (the roughness under which it sees emission) uses reflection, any other's indirect.
- **Draws carry the flags:** `DrawItem::seenByCamera`, `seenInReflections`, `seenByIndirect`, with `castsShadows` as it was. A draw the camera does not see is left out of the G-buffer and the glass pass. Collision follows shadows, as particles collided with shadow casters before, so the player's weapons, which cast no shadow, still stop no spark.
- **Entities set them for what is under them:** `scene::RayVisibility` on an entity applies to its meshes and those of its descendants, the nearest one up the hierarchy winning, so a whole model is hidden by one component at its root. Level files keep it as `visibility` (`camera`, `shadows`, `reflections`, `indirect`); the inspector has a Ray visibility section; game code adds or changes the component.

Measured on an RTX 3090: a block hidden from the camera, its shadow still on the floor, and a block that casts no shadow, are 0.005 and 0.006 FLIP from the path tracer with the same masks. A block seen by neither the camera nor bounced light shows in a polished metal floor: 0.088 FLIP from the path tracer, against 0.132 without it.

## Consequences

- An entity hidden from the camera still lights the scene if it is emissive: emission is a light whatever sees its mesh.
- Particles are not in the acceleration structure yet (step 11.7): they are seen by the camera only, whatever these flags say.
- The radiance cache and the bounce rays share the indirect bit; a separate bit for each would be one more line.

## Alternatives considered

- **Visibility as material properties:** per material rather than per entity, so the same model could not be a mirror ghost in one place and an ordinary object in another.
- **Ray flags at the hit (an any-hit test per kind):** works without masks, but the engine traces inline with ray queries and masks are free in hardware; testing at each candidate hit costs on every ray.
