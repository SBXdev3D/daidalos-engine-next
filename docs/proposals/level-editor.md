# Proposal: a better level editor

*2026-11-20. Not approved, not started. Built from the assessment's editor list (`docs/ASSESSMENT.md`), the owner's requests and a comparison with Unreal Engine, Unity and Godot. Sizes are guesses from reading the code, not plans.*

## Where the editor is

The editor (`apps/editor`, about 5,200 lines) draws its viewport with the game's renderer. It has a hierarchy, an inspector, gizmos with snapping, undo by whole-level snapshots (ADR-0055), copy and paste, bookmarks, an asset browser, prefabs, painting, a material editor, a sequence timeline, autosave, and play in a separate process with F5.

Two facts from the code shape this proposal:
- **Opening a level blocks the editor.** `Editor::Open` loads every model on the main thread before it draws again. The game's sandbox already loads models on workers behind a loading screen (`apps/sandbox/src/Main.cpp`, `LoadLevel`), so the work is already solved once.
- **There is no unlit view.** `renderer.debug_view` has 14 views (normals, indirect, lights, fog and others), but none shows surfaces without lighting. The factory is dark on purpose, which makes placing things in it hard.

## A. Owner's requests

1. **A loader while a level opens.**
   - Load models on workers as the sandbox does, and build the entities when they are all there.
   - Meanwhile the editor keeps drawing: a progress bar in a modal ("Factory.level.json: 23 of 41 models"), the name of the file being read, and Cancel, which goes back to the level that was open.
   - Use it for every path that loads: Open, Reload from disk, Recover after a crash, New from a template, and the editor's first level.
   - Share the code with the sandbox instead of copying it: move the worker load into `scene` (something like `scene::LevelLoader` with `Progress()` and `Done()`), and have both apps call it.
   - Size: small to medium. The risk is entities or the picker being touched before the load ends. Disable editing until `Done()`.
2. **Lighting on and off**, as a menu in the viewport's toolbar, like Unreal's view modes and Unity's scene-lighting button:
   - **Lit:** the game's image, as now.
   - **Unlit:** base color only, from the G-buffer. A new `DebugView::BaseColor` that reads the existing G-buffer, so it costs nothing.
   - **Editor light:** the level's lights off, with one soft light that follows the camera, plus a little ambient. This is the most useful for placing props in the dark. It is editor-only state, never saved in the level.
   - **Lighting only:** light on a white surface, to judge the light without the textures. It is the same pass with base color forced to white.
   - The existing debug views (normals, lights, fog, wetness and others) go in the same menu, so nobody has to type cvars.
   - One shortcut cycles Lit, Unlit and Editor light (Unreal uses Alt+number keys; F2 to F4 or similar here).
   - Size: small. Unlit and Lighting only are shader switches. Editor light is a scene light owned by the editor, plus a flag that skips the level's lights in the light list.

## B. From the assessment, ranked by what level designers feel most

3. **Undo for every edit.** Materials and sequences currently go back to their files. The history should hold any document, not only the level.
   - Keep ADR-0055's snapshot idea per document (a material is small, a sequence is small): one history with entries that name their document.
   - Recommended *before* play-in-editor, because play-in-editor adds edits made while playing that will need undo too.
4. **Play in the editor:** play inside the viewport with the scene paused, stepped and inspected live, then return to the level as it was.
   - It needs the game's code to run in a copy of the editor's scene and be dropped after. ADR-0055 chose a separate process to avoid exactly that.
   - A middle step costs much less and covers most of the need: **Simulate** (Unreal's term). Physics, particles, lights and sequences run in the viewport without the player or the game rules, and Stop restores the level from the history's snapshot.
   - The largest item in this proposal. It needs its own ADR.
5. **Orthographic top, front and side views**, and a 2x2 split view. Also: wireframe in all of them, and a frame-the-selection key in each.
6. **Snapping to surfaces and to vertices:**
   - drag an object and it lands on the surface under the cursor, aligned to its normal (Unity's Shift+Ctrl drag, Unreal's surface snapping);
   - hold V to snap a vertex to another vertex (both engines);
   - **Drop to floor** with End, as in Unreal, using the physics world's ray.
7. **Prefab overrides:** copies that differ by some fields and keep the rest from their prefab, edited in place, with nesting.
8. **Context menus** in the viewport and the hierarchy, and **search everywhere** (entities, assets, settings, menu commands).
9. **An effect editor** with a live preview of `.effect.json` and `.ribbon.json`, curves edited as curves.
10. **Simple geometry for blocking out:** boxes, stairs, ramps and cylinders with collision, textured with a grid material, to try a room before modelling it in Blender (Godot's CSG, Unity's ProBuilder, Unreal's modelling tools).

## C. More gaps found by comparing with Unreal, Unity and Godot

### Seeing the level
11. **Show flags:** turn categories on and off in the viewport: light icons, fog volumes, triggers, wet areas, wind volumes, decals, particles, collision, portals. Unreal's Show menu and Godot's gizmo toggles. Also a **game view** key (G in Unreal) that hides all editor drawing.
12. **Overlays drawn in depth.** The grid and markers show through walls today (ADR-0055 deferred it). With many triggers and lights in a room, that turns into clutter.
13. **Light gizmos that show their reach:** the range sphere of point lights, the cone of spots, the falloff. Also a **light complexity view**: how many lights touch each pixel, which the ReSTIR budget cares about.
14. **More views:** collision (physics shapes), the navigation mesh (`engine/navigation` already builds one), portals and rooms (ADR-0089), and audio zones and reverb.
15. **Sound in the editor**, off by default with a toggle: ambient sounds and emitters near the camera. The editor is silent today, but in a horror game the sound placement is half the level. Unreal plays it in the viewport.
16. **Real-time toggle:** pause the viewport's time (particles, flickering lights, sequences) for a still frame to place things against.

### Organising and editing
17. **Hide, lock and isolate** per entity and per hierarchy branch, plus **folders** in the hierarchy. All three engines have them. Locking stops a click from picking the floor instead of the prop on it.
18. **Pivot editing and align:** move the pivot, align or spread the selection along an axis, and rotate around the surface normal.
19. **Scatter:** paint many copies of a few props on a surface with random rotation and scale (Unreal's foliage, Unity's terrain details). The paint tool (ADR-0078) is the start of it.
20. **A measuring tool:** a ruler between two points in metres, since the engine uses physical units (ADR-0043).
21. **A camera preview:** a small picture of what a selected camera or sequence shot sees (Unity's camera preview, Unreal's cine camera).

### Checking and assets
22. **Map check:** a list of problems with a click to go to each one (Unreal's Map Check). For example: a missing model or texture, a trigger with no target, a sequence that names a deleted entity, a light inside a wall, an entity outside every room, a player start missing or duplicated.
23. **Level statistics:** entities, lights, triangles, draw calls, particles, and memory by kind. Compared against the preset budgets with a warning colour (Unreal's Statistics window, Godot's monitors).
24. **Asset references:** who uses this model, material or effect, and a safe rename or move that updates the files that use it (Unreal's reference viewer and redirectors, Godot's dependency fixing).
25. **Import settings per asset,** kept beside the asset: scale, collision, texture format (Unity's import inspector).

### The interface
26. **A tidier theme:** spacing, icons on toolbars, a dark theme close to the commercial editors' look, and a saved layout per user. Dear ImGui can do it, mostly with fonts and icons.
27. **Remappable shortcuts** and a **command palette** (Ctrl+P), which item 8's search can serve.
28. **Several documents in tabs:** a level, a prefab and a material open at once, each with its own history (needs item 3).

## D. Not recommended now

- **Node-graph materials and visual scripting** (Unreal's Blueprints, Unity's Shader Graph). Each is a project on its own. The game's needs are covered by material parameters, sequences and C++.
- **Terrain tools.** The game is indoors.
- **Level streaming and World Partition.** One level fits in memory, and portals already limit what is drawn.
- **Several users editing at once.** One owner.

## Suggested order

1. A loader (1) and the lighting menu (2). Both small, both asked for, and they make every later step nicer to test.
2. Show flags, game view and overlays in depth (11, 12), and hide, lock and folders (17). Small, and felt every minute.
3. Snapping to surfaces, Drop to floor, vertex snap (6), and orthographic views (5).
4. Undo for every document (3), then tabs (28).
5. Simulate in the viewport, then full play in the editor (4), behind an ADR.
6. Map check and statistics (22, 23), then context menus, search and the command palette (8, 27).
7. Prefab overrides (7), the effect editor (9), blocking-out geometry (10), then the rest of section C.
