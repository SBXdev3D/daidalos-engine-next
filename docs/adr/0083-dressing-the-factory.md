# ADR-0083: Dressing the factory, and static bodies

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Step 16.2 of the showcase (ADR-0082) puts the kit of props into the factory's rooms. Two things were missing:

- **The props were walk-through.** The sandbox makes collision geometry from the model nodes tagged `level` only. Props placed by the level file would stop neither the player nor shots nor sound, and zombies would walk through them.
- **Two hundred props have to be placed by hand or by rule.** The editor places one at a time, from what the view shows. Two hundred props against walls, clear of doors, ladders, pickups and triggers, under enough ceiling, is a rule.

## Decision

### A body without mass is a static box

- `Body::mass` of 0 makes a static box instead of a loose object (ADR-0073). It sits where the entity is in the world, parents included, as `StaticBox`: its eight corners and its physics body. It is removed with its entity.
- The sandbox gives static boxes to navigation, so zombies walk around props. It gives them to sound, so props block it with their model's material, and records their surface, so shots throw up what the prop is made of.
- A box rather than the mesh: the kit is 634,000 triangles, which would make a slow physics world and a large navigation input, while a crate's collision only needs its box. A prop that needs a better shape can be split into several boxes as entities.
- Inside a prefab, parts are parented after they are made, so a static box there would sit at the prefab's origin. Props with bodies are placed directly in the level.

### The dressing is a script the editor checks

`scripts/dress_showcase.py` reads the level's geometry from `Factory.gltf` into a grid of 25 cm cells:
- floors: faces that look up;
- ceilings: faces that look down;
- walls: vertical faces between knee and head height.

It lists fourteen rooms, each with a purpose and what tells it:

| Room | Purpose |
|---|---|
| Start room | maintenance bay |
| Hall | loading floor under an overhead crane |
| East room | workshop |
| Upper floor | offices and stores |
| Yard | open air, with barrels and gas bottles |
| Underground | pumps, and the room under the hall |

Rules decide each placement:
- **Props** stand with their backs to a wall, turned a few degrees at random. They need floor under their whole footprint and enough ceiling over it. They keep a walkway of 1.2 m in front, 2.5 m around doors, 1.6 m around what the game uses (pickups, triggers, effects, spawns) and clear of ladders. "Cluster" items stand next to the previous one: rows of barrels, stacks of crates.
- **Lamps:** hanging lamp prefabs hang from ceilings, 2 m from walls and 6 m apart. Wall lamps sit on walls at 2.5 m.
- **The crane** spans the hall under its usual ceiling.
- **Painting:** grime, mold, blood and scorch decals, and wet patches, go near walls.
- **Collision:** props larger than a stool get a static box.

Each room's additions hang from one entity named `Dressing: <room>`, which the next run replaces, so the rules can change and run again. The editor rewrites the file in the engine's layout, and is where a placement gets corrected by hand.

The kit's materials that Poly Haven marks as blended are not drawn by the engine (ADR-0042 draws glass with transmission, not blending). The fetch script now makes their glass real glass and cuts the rest out at half alpha, so lamp glass no longer covers its bulb.

## Consequences

- The factory holds 228 more things.
- About 1.6 million triangles are drawn, against 20,000 before. The frame took 1 to 1.5 ms more on the GPU in the first measurements at 720p. Step 16.6 measures it properly, against ADR-0081's thresholds for meshoptimizer.
- Props are boxes to physics: a shot at the gap between a shelf's legs stops at the box.
- A level edited by hand in a dressing group is overwritten by the next run of the script: hand edits belong outside the groups.

## Alternatives considered

- **Mesh colliders for props:** exact, and 30 times the triangles of the level for physics and navigation.
- **Placing everything in the editor by hand:** the editor's intended use, and a day of clicking for a first pass that rules do in a second. The editor stays the tool for the second pass.
- **Engine-side procedural dressing at load:** levels would change with the rules, and a level file would no longer say what is in the level.
