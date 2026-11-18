# ADR-0079: The material editor and the sequence editor

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 grows the editor with a material editor with live preview, and with trigger editing (ADR-0041). ADR-0078 did prefabs and painting and left these two for step 15.5.

Until now:

- A material's values (color, roughness, porosity, relief, clear coat and the rest) could be changed only in Blender or in a script that rewrites glTF files, and seen only after reloading the level.
- A trigger's scare is a `.sequence.json` timeline (ADR-0071). It could be written only by hand in a text editor, and seen only by walking into the trigger in the game.

## Decision

### Materials stay in glTF, edited live

- **No material format of the engine's own.** ADR-0041 keeps materials in glTF and its `KHR_materials_*` extensions. The material panel edits a model's materials and writes them back into the model's `.gltf` file. `.glb` files are refused: their JSON sits inside a binary container.
- **Live:** `Renderer::UpdateMaterial` rewrites a material's slot in the table every shader reads, and its pass flags (glass, alpha test, double-sided, emissive). The raster passes, the ray-traced effects and the path tracer all show the change from the next frame. An emissive material's triangles are averaged again (ADR-0044), so a lamp made brighter lights the room more.
- **Models keep their materials:** `Model::materials` holds, for each glTF material, its handle, its values (`resources::MaterialData`, as the file gives them) and what the renderer was given. `Part::materialIndex` ties a part to its material. `AssetLibrary::UpdateMaterial` changes one for every entity placing the model. A click in the view shows the material of the surface clicked; selecting an entity shows its model's materials.
- **What is edited:** base color, metallic, roughness, normal and occlusion strength, alpha cutoff, double-sidedness, emission as a color and a luminance in nits, whether the emission lights the scene, clear coat, sheen, glass (transmission, index of refraction, thickness, absorption), what the surface is made of, porosity and relief height. Textures are shown but not swapped: they belong to Blender and texture-tool.
- **Writing back:** `scene::WriteGltfMaterials` parses the document into the JSON tree the level files use, now shared as `JsonTree`. It sets the core factors, adds or takes out each extension as its values need (an extension that holds a texture stays), keeps `extensionsUsed` in step, and writes the extras `light`, `surface`, `porosity` and `height`. Emission over 1 goes into `KHR_materials_emissive_strength`, as the importer reads it. Everything else in the file is kept, but the file is written with sorted keys and the level files' number format. The first save of a file made by Blender or a script therefore reformats all of it, once.
- **Not in the level's undo history:** material edits change other files than the level. The panel marks changed materials, Revert puts back what the file holds, and Ctrl+S saves them with the level. Unsaved materials count as unsaved changes when the editor closes or opens another level.

### Sequences edited as timelines

- **`WriteSequence`** writes a sequence file: cues in time order, each with the fields its kind uses, keys sorted. The file's `comment` is now kept as `Sequence::comment`, so a sequence read and written again is the same file.
- **The sequence panel** shows a sequence as a timeline. Each cue is a marker colored by its kind, with a bar for what lasts (a fade, a shake, a camera move, a subtitle). Markers drag in time (Ctrl: by tenths of a second). Beside the timeline is the list of cues, and the selected cue's fields. Entity fields offer the level's names. A camera key can be set from the editor's view, and a move cue can go to where the view looks. The selected camera cue's path and the lines to where its keys look are drawn in the view, as is where a move cue goes.
- **Open:** a trigger's inspector has "Edit its sequence". A trigger without one gets `Sequences/<name>.sequence.json`, relative to its level, made when the panel saves. Sequence files in the asset browser open with a double-click, and `+editor.sequence <path>` opens one at start.
- **Preview:** Play runs the sequence in the editor's scene as it is, saved or not. Lights, power, particles, moves and visibility play out; the editor's scene has no sound. "Put the scene back" makes the level's entities again, undoing what the preview changed. A red playhead follows the preview on the timeline.
- **Not in the level's undo history** either: Revert reads the file again, and Ctrl+S saves the sequence with the level.

## Consequences

- The first save of a material into `Factory.gltf` rewrites the whole file in the engine's layout: one large diff, then small ones.
- An edited material changes everywhere its model is placed, and in every level using that model. That is what a material in a shared file means, and why there are no overrides per level.
- A material shared by two models lives in two files and is edited twice.
- The preview plays a camera cue through the scene's cinematic camera. The editor does not yet show it through that camera, so a camera move is judged by its drawn path.
- Step 15.6 is the content pipeline and shipping.

## Alternatives considered

- **Material overrides in level files:** no glTF rewriting, edits per level. But the values would then live in two places, and a model re-exported from Blender would keep values it no longer shows. ADR-0041 chose glTF as the one source.
- **A material file of the engine's own** (a `.material.json` per material, as engines with a material graph have): it leaves Blender's materials behind, for no gain while materials are parameters rather than graphs.
- **Rewriting only the changed bytes of the glTF file:** keeps a file's layout. It needs a JSON parser that keeps positions, which simdjson does not, for a one-time cost.
- **Sequence edits in the level's undo history:** one Ctrl+Z for everything. But the history holds levels; it would have to hold other documents too. A sequence is short enough to revert.
