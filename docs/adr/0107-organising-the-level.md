# ADR-0107: Hiding, locking, folders, align, pivots and measuring

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.3 of Phase 20 (ADR-0104): proposal items 17, 18 and 20. The factory has about 600 entities in one flat hierarchy, grouped only by parents. A click on a prop often picks the floor or the room around it, nothing can be set aside while working on something else, rows of lamps are lined up by typing numbers, a group can only turn around its first entity's origin, and nothing measures a distance.

## Decision

- **What only the editor reads is kept in the level file**, as `"editor": {"folder", "hidden", "locked"}` on an entity (`scene::LevelEditing`). It is written only when not the default, ignored by the game, and changes nothing in the scene (`LevelInstance::Sync` treats it as a move of nothing). It is saved, so a level opens as it was left, and it is part of the history, so hiding and locking undo like any edit.
- **Hide (H) and show everything (Alt+H).** The editor's scene is made from the level without its hidden entities and what is under them (`ShownLevel`). Every edit, undo and preview syncs the scene from what showed before to what shows now, so a hidden lamp's light goes out too, and nothing hidden can be clicked.
- **Isolate (Shift+H)** shows only the selection, what is under it and what holds it, until Shift+H again. It is not saved.
- **Lock (L).** A locked entity, and what is under it, is passed through by clicks in the view (the picking ray skips it; `MeshPicker::Cast` takes a filter) and has no gizmo. It is still chosen in the hierarchy and edited in the inspector, so the floor can be locked and the props on it clicked.
- **Folders.** An entity at the root can be in a folder of the hierarchy, a path such as `Lights/Hall`.
  - The hierarchy shows folders as tree nodes, before the entities at the root.
  - Dropping an entity on a folder puts it there, out of any parent, where it stands in the world. Dropping it below the list takes it out.
  - Edit > Put in a folder... does the same for the selection.
  - Clicking a folder selects what it holds.
  - Each row, folders included, ends with a hide button (o shown, - hidden) and a lock button (L locked).
- **Pivots** (toolbar: Pivot): the gizmo sits at the primary entity's origin, at the center of the selection's box, or at the bottom center of it, turned as the primary is. The selection turns and scales around it. The gizmo's motion since the drag began is applied to where each entity was then, so a long scale or turn of many entities neither drifts nor compounds.
- **Align and spread** (Edit menu): line the selection's roots up on X, Y or Z at their lowest, middle or highest; or spread them evenly between the two farthest, keeping their order. Each is one step of the history.
- **Measure (M or the toolbar):** drag between two surfaces in the view. The line shows its length and its X, Y and Z parts, with three faint legs along the axes. It snaps to the move step while snapping is on. M or Escape ends it.

## Consequences

- A level file may now carry `editor` objects; older builds of the game reject unknown keys, so levels saved by this editor need this build or later. The game in this repository is built with it.
- Hidden entities are not in the editor's scene. A preview of a sequence that names one does not see it.
- Long names run under the hide and lock buttons; a tidier hierarchy comes with the interface's rework (step 20.14).

## Alternatives considered

- **Hidden and locked kept beside the editor, not in the level:** the file would stay the game's alone, but a level shared or opened on another machine would lose how it was organised. Unreal keeps folders and the hidden flag in the level too.
- **Folders as empty parent entities**, as Unity does: no format change, but moving an entity into a folder would change its transform's parent, and the game would make an entity for every folder.
- **Hiding by leaving entities out of the frame** in `OnRender`: they would still be clicked, and a hidden lamp would still light the room.
