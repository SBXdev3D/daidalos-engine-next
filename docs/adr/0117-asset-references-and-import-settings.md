# ADR-0117: Asset references, safe moves, and import settings

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.13 of Phase 20 (ADR-0104): proposal items 24 and 25. Assets are named by path, relative to the level, prefab or effect that names them. Renaming or moving one in the file manager broke every file naming it, and the only way to find which did was to search their text. A model's size and collision came only from its file: a model made in centimeters, or a wall without a `level` node, meant editing it in Blender.

## Decision

- **What uses it.** An asset's right-click menu in the Assets panel lists every file under `assets/` that names it, and where in it (`Hum's sound`, `its texture`, `a cue at 2.50 s`). The files read are levels, prefabs (every path field, prefab overrides included), effects and ribbons (texture, mesh and sub-effects) and sequences (cue clips and effects). Texture sources are left out (`Source` folders). This is Unreal's reference viewer, as a list.
- **Rename or move.**
  - The asset's menu takes a new path under `assets/`, moves the file with its import settings, and rewrites every file that named it to name the new one. Paths stay relative to each file.
  - The level open in the editor is repointed in the editor, as one step of the history, and saved with the rest. Its file on disk is not rewritten under unsaved changes. A move waits until other tabs are closed.
  - A model whose buffers or images lie beside it can be renamed in its folder but not moved out of it: moving its folder is a file-manager job.
- **Import settings per model**, in `<model>.import.json` beside it, so the model's file stays as Blender wrote it:
  - a scale (0.01 for centimeters), applied to its root nodes when it loads;
  - `level_collision`: the game makes all of its parts walls and floors, as it does nodes named `level` (ADR-0083).

  The asset's menu edits them; the defaults remove the file. Levels opened afterwards show the change, since models are loaded once.

## Consequences

- Rewritten files are written in the editor's layout: levels and effects with sorted keys.
- Six-faced skies are named by a prefix, not a file, and are not moved by the menu.
- The settings apply in the game too: they belong to the model, not the editor.

## Alternatives considered

- **Redirector files at the old paths,** as Unreal leaves: nothing rewritten, but files that point elsewhere pile up and every load follows them.
- **Settings inside the glTF's extras:** one file, but each import from Blender would lose them.
