# ADR-0082: Phase 16, the factory showcase

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phases 9 to 15 built a ray-traced renderer, a level editor, materials and atmosphere, sound, gameplay systems and the production side. Phase 16, agreed with the owner on 2026-11-16, rebuilds the factory demo in the editor with all of it, before Phase 17 hardens everything and an assessment closes the run. The owner's direction (`docs/DEMO.md`) is Doom's pace and an oppressive horror atmosphere.

A screenshot tour of the factory at the start of the phase, at eye height with the flashlight on, shows what is wrong. The level is still the legacy engine's: boxes of tiled floor and brick wall, a few pickups, and almost nothing else. The rendering and the systems show in the lights, the fog, the rain and the alarms. They have nothing to fall on: no machines, pipes, shelves, crates, cables or clutter. A showcase is first a place.

Content must be free to use and to show: CC0 only (Poly Haven, ambientCG, Quaternius), each with its source and licence recorded (decided 2026-11-16).

## Decision

Phase 16 is six steps, each one PR:

1. **16.1, a kit of props.** About 35 CC0 models from Poly Haven, chosen for an underground factory:
   - barrels, jerrycans, crates;
   - steel shelves, tool chests and carts;
   - power and utility boxes, a compressor and a generator;
   - modular pipes, ducts and cables;
   - fluorescent, caged and hanging lamps;
   - signs and clutter.

   `scripts/fetch_showcase_props.py` downloads their glTF files with 1k textures. It converts each texture to KTX2 with `texture-tool`, keeps only the default material of each part (Poly Haven's variants are dropped), and writes what each material is made of into its extras. It records each model's authors and licence in `assets/Factory/Showcase/LICENSE.md`. Only the converted files are committed. The kit's lamps become prefabs with their lights (`scripts/make_showcase_prefabs.py`): each bulb glows at a real lamp's luminance without lighting anything itself, and the prefab's spot or point light does the lighting, on a circuit if the level wants. The fluorescent fixtures come as one model of several and stay a prop. The kit's triangle count is measured against ADR-0081's thresholds for meshoptimizer.
2. **16.2, dressing the rooms.** Each room gets a purpose its props tell: the start room a maintenance bay, the hall a loading floor under the crane, the west wing storage, the east wing offices and lockers, the north rooms the control room, the underground the pumps and fans. Lights move to the kit's lamps, on power circuits, and some fail. Grime, blood, rust and puddles are painted on with the editor's paint tool. Every room is checked from the player's eyes, before and after.
3. **16.3, pacing and scares.** Each area has a sequence: a light that dies as the player passes, a sound behind a door, an ambush when a door opens, a stinger, a shake. There is a clear exit, and an end-of-level summary of kills, items and time (`docs/DEMO.md`).
4. **16.4, feel.** A screen flash and a direction when hurt. Hit feedback on zombies. Stronger recoil and sway. Footsteps and breathing that follow the player's state.
5. **16.5, the opening.** The legacy demo's opening camera flight, as a camera sequence over the dressed level, before the player takes control. A set of showcase screenshots from fixed places, compared with the tour taken at the start of the phase.
6. **16.6, the showcase's budget.** GPU and CPU times on every quality preset in the dressed level. The fixes this needs come here, or are listed for Phase 17.

The editor makes the levels, as Phase 10 intended. Where an edit is mechanical and repeated (placing a hundred props along walls), a script writes the level file and the editor checks it. Both write the same file.

## Consequences

- The repository grows by the kit: 75 MB of KTX2 textures and meshes for 38 models (634,000 triangles in all, the overhead crane the largest at 90,000), with 1k textures for the nine large pieces and 512 for the rest (`texture-tool --max-size`). The JPG sources are not committed.
- The level's look changes much: the tour taken at the start of the phase is kept to compare with.
- If the kit's models cost what ADR-0081 set as the threshold, meshoptimizer comes in 16.1.
- Phase 17 hardens the showcase rather than the bare level.

## Alternatives considered

- **Modelling the props in Blender by script:** everything would be the project's own, but simple shapes look like simple shapes. CC0 models made by artists show the renderer better, and Blender stays available for what the kit lacks.
- **A new level:** a showcase without the legacy route. The factory is the demo the project remakes, and its route is the one the player knows.
- **Downloading the kit at build time** instead of committing it (as `fetch_sample_assets.py` does for the sample models): a smaller repository, but a demo that needs a network to build, and an archive built from whatever the site serves that day.
