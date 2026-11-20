# ADR-0111: Map check and level statistics

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.7 of Phase 20 (ADR-0104): proposal items 22 and 23. A level's mistakes showed only when the game ran into them: a missing model logged at load, a trigger whose sequence named a renamed lamp doing nothing, a pasted crate left on top of its original. Nothing told a level's cost against what the renderer draws at once (64 decals, 16 wet areas), or its GPU time, without the developer overlay.

## Decision

- **Map check (Check > Check the level, Ctrl+Shift+C, the Map check panel)**, as Unreal Engine's Map Check. It runs when the panel first shows a level and on request, and lists problems as errors, warnings and notes. A click selects the entity and frames it. It is a pure function, `CheckLevel(level, levelPath, sources)`, given file existence and sequence reading as functions, so it is tested without files. It finds:
  - ids used twice, parents missing or holding each other;
  - files named that are not there: models, prefabs, effects, ribbons, decal images and normal maps, light profiles and cookies, the sky's faces, trigger sequences. They are named by their path under `assets/`;
  - a trigger with no sequence, and a sequence whose light, move or visibility cues name entities the level does not have;
  - a scale of zero, and a light with no intensity or no range;
  - two copies of the same model or prefab, under the same parent, in the same place: a paste or duplicate left where it was;
  - no player start, or more than one;
  - with rooms (ADR-0089), entities outside every room, as **notes**, hidden unless asked for. The factory's rooms cover part of it on purpose, and what lies outside is drawn from everywhere.
  - In the editor, using the scene: a light closed in within 25 cm on five or six sides, likely inside a wall. Rays from the light pass through its own lamp, since a shade or a cage would close it in.
- **Statistics panel.**
  - The GPU frame against 60 frames a second, with its six costliest passes.
  - Draws in view and drawn, triangles, lights, and particles alive.
  - The level's entities by kind, and lights by type.
  - Decals and wet areas against the 64 and 16 the renderer draws at once.
  - GPU memory against the budget the driver gives.
  - Textures, buffers, meshes and materials, and the models and images loaded for the level.
  - Bars turn amber past 80% and red past 100%.

## Consequences

- The factory has no errors or warnings; about a hundred notes list what lies outside its ten rooms.
- The GPU budget is this machine's. The presets' budgets (ADR-0087) are measured at 1080p in the game, and the editor's view is smaller and draws overlays.

## Alternatives considered

- **Checking as the level is edited:** problems shown at once, but a check reads every sequence file and casts six rays per light; on request, and when the level opens, is enough.
- **Light inside a wall by the light's sphere against triangles:** exact but costly, and lights touching a wall are common. Closed in on most sides is what matters.
