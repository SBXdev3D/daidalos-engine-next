# ADR-0113: Prefab overrides, edited in place

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.9 of Phase 20 (ADR-0104): proposal item 7. Prefabs (ADR-0078) placed their file's entities as parts of the entity placing them, all copies alike. A lamp prefab placed ten times, one of them dimmer or missing its cage, had to be unpacked, after which it no longer followed its prefab. ADR-0078 chose unpacking over overrides. The engine already nests prefabs, eight deep, but the editor only edited them by opening their file.

## Decision

- **An entity placing a prefab keeps a list of overrides** (`scene::PrefabOverride`), one per changed part. Each names the part by its id in the prefab's file and says only what this copy changes:
  - its transform;
  - its light (the whole component);
  - its model;
  - or that it is left out, with what hangs from it.

  Everything else comes from the prefab, so a change to the file still reaches every copy, except where a copy said otherwise. They are written as `"overrides": [{"part": 3, "light": {...}}]`.
- **`ApplyOverrides(prefab, overrides)`** gives the prefab's entities as one copy places them. `LevelInstance` places those. Overrides naming a part the prefab no longer has are ignored. Unpacking a copy keeps its changes.
- **Editing in place.**
  - The inspector of an entity placing a prefab lists the prefab's parts; a changed part shows `*`.
  - Opening a part edits it for this copy: left out or not, its position, rotation and scale, its model, and its light with the light's own fields.
  - What differs from the prefab becomes the part's override (`OverrideOf`); "Back to the prefab" drops it.
  - A click on a part in the view selects the copy and opens that part in the inspector (`LevelInstance::PartOf`).
- **Apply to the prefab** writes a copy's overrides into the prefab's file for every copy, clears them, and makes the copies read the file again, as Unity's "Apply". "Back to the prefab, all of it" drops them.
- **Nesting:** a prefab placing prefabs is made by Make prefab from a selection that holds copies; their overrides go into the new file with them. A copy's overrides reach its own parts, among them the copies the prefab places (moved, or left out); deeper changes are made in the inner prefab's tab (ADR-0109).

## Consequences

- Override model paths are relative to the prefab's file, as its parts' are.
- A light override replaces the whole light, so a later change of the prefab's light color does not reach a copy whose light was changed. Overrides field by field are left for when a level needs them.

## Alternatives considered

- **Overrides field by field, as Unreal and Unity keep them:** finer, but each field of each component needs its path and merge. The parts that differ between copies in the factory differ in where they are, their light, or their being there.
- **Copies as unpacked entities with a link back:** each copy would keep a full copy of the prefab, and changes to the file would need merging into them.
