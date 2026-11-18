# ADR-0078: Prefabs and painting in the editor

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 grows the editor "by need" (ADR-0041): prefabs, painting wetness and decals on surfaces, a material editor with live preview, and trigger editing. ADR-0054 left prefabs to this phase: a level had no includes, so the factory's two alarms each carried a copy of the same two red beams. Decals and wet areas could only be placed one box at a time, from the Create menu, turned by hand to face a wall.

All four are too much for one step. This step does the two that change what a level file holds or how the level is dressed: prefabs and painting. The material editor and the sequence editor (trigger editing) are step 15.5. The pipeline and shipping move to 15.6.

## Decision

### A prefab is a level file that other levels place

- **`<name>.prefab.json` has a level file's format** (ADR-0054). It is read by the same parser and edited in the editor as any level. Its environment is not used.
- **An entity places a prefab with `"prefab": "<path>"`**, relative to the level, as `model` places a glTF file. The two can be on the same entity: the factory's alarms keep their own model, rotation speed and game tags, and place `Prefabs/alarm_beams.prefab.json` for their beams.
- **The prefab's entities are made under the placing entity as parts.** `LevelInstance` builds them through an instance of its own, which gives them no `LevelEntityId`. A click on one selects the entity placing it, as a click on a model's node does. They are not entities of the level, have no ids in it, and are not saved with it. The game sees their components and tags as usual, and save keys come from names (ADR-0072), so a pickup inside a prefab still saves.
- **No overrides.** A prefab placed ten times is the same ten times. To change one copy, Edit > Unpack prefab copies its entities into the level under the placing entity, which no longer places it. This follows ADR-0054, which rejected overrides of a model's nodes. Overrides come when a level asks for them: the factory's lamps differ in light from room to room, so they are not a prefab.
- **Prefabs place prefabs**, up to eight deep. A prefab that places itself, directly or not, is refused with a warning.
- **Read again when changed:** `AssetLibrary::LoadPrefab` keeps a prefab's level until its file changes on disk. `LevelInstance::RemakePrefabs` makes the placing entities again (View > Read the prefabs again), for a prefab saved by another editor.
- **In the editor:** Edit > Make prefab saves the selection and what is under it as a prefab, under new ids. The selection's roots are placed around the primary selection, which becomes the prefab's origin. The selection is then replaced by one entity placing the prefab, where the primary was. Prefabs show in the asset browser, to place like models, and in File > Open. The inspector opens the prefab of the selected entity, or unpacks it.
- **Relocate** (Save As) moves prefab paths, and now also the sequence of a trigger, which it missed.

### Painting stamps entities

- **The paint tool (B, or Paint in the toolbar) stamps decals or wet patches** where the left button drags over surfaces. A new stamp comes every *spacing* meters. Each stamp has a size jitter and, if chosen, a random turn about the surface's normal.
- **A stamp is an entity**, a decal or a wet volume as Create makes. It needs no new component, no texture painted per level and no change to the renderer's material function. The stamps of one stroke hang from an entity of their own ("Painted grime"), which moves, hides and goes as one. A stroke is one step of the undo history.
- **Stamps face the surface:** CPU picking now returns the hit triangle's normal. A decal's box straddles the surface and projects along its -Z into it, upright on walls. A wet patch's box stands on the surface and reaches 10 cm below it, so that water pools a little above the floor.
- **Brushes:** the decal images are those in the level's `Decals` folder. A `<name>_normal.png` beside an image is taken as its normal map. Decal images appear in the asset browser. A double-click there takes the image for the brush; a drop on a surface stamps one decal there.
- **More than 16 wet volumes:** a painted level easily has more wet patches than the 16 the renderer draws. It now draws the 16 nearest the camera, as it already did for decals (64), instead of the first 16 in the scene's order.

## Consequences

- The factory's two alarms place one prefab of beams: `Factory.level.json` loses four entities.
- A painted stroke is many small entities: a wet stroke of ten stamps is ten wet volumes. Wetness in the hierarchy is easier to reason about than in a painted mask, but a whole floor is still better done with one large wet area. Beyond 16 wet volumes in view, the farthest disappear.
- Prefabs' models are loaded when their entity is made, not by the loading screen's workers (`LevelModels` reads no prefab files). The factory's prefab has no model. A prefab with heavy models will want its models listed.
- A prefab's parts have no ids, so a sequence or a ribbon in the level cannot target an entity inside a prefab.
- Found while testing this step: since 13.3 the scene gave its sound world an empty asset root (a member initialized from a moved path), and the game played no sound. It is fixed, a test loads a sound through the scene, and MSVC now warns about members initialized out of order (C5038).

## Alternatives considered

- **Prefabs with overrides per field** (Unity's and Godot's): the most flexible, at the cost of a second identity inside every placed prefab and of merging rules. Not needed by the factory.
- **Prefabs copied into the level when placed:** no new file format, but an edit to the prefab then reaches no level.
- **Painted masks:** a wetness or grime texture per surface, painted in the viewport. Exact and cheap to draw, but it needs a second UV set or a virtual texture per level, and a material function that reads it. Stamps reuse decals and wet volumes as they are. ADR-0061 already deferred a painted wetness map to the first asset that needs one.
