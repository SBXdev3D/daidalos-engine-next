# ADR-0054: Level files

- **Status:** Accepted
- **Date:** 2026-11-16

## Context

Until now a level was a glTF file plus the code that builds on it (ADR-0017). The game read the names of the model's nodes, `kind;key=value;...`, and built lights, fog, emitters, weather, the sky and its moon, and the alarms' lights, some from the names and some from constants in its code. Phase 10 builds a level editor (ADR-0041): what it edits must be data, saved in a file the game loads, and drawn by the editor with no game code.

## Decision

### A level is a JSON file, `<name>.level.json`

```json
{
  "format": "daidalos-level",
  "version": 1,
  "environment": {
    "sky": { "faces": "Sky/sky1", "intensity": 9.6, "yaw": 180 },
    "fog": { "density": 0, "albedo": [0.8, 0.82, 0.85], "anisotropy": 0.1 },
    "exposure": { "ev100": 3, "auto": true, "min": -0.5, "max": 2.5 },
    "wind": { "velocity": [0, 0, 0], "gust": 0, "gust_frequency": 0.2 }
  },
  "entities": [
    { "id": 1, "name": "Factory", "model": "Factory.gltf" },
    { "id": 7, "name": "Alarm west", "transform": { "translation": [-10.5, 8.4, -47.25] },
      "model": "Props/alarm.gltf", "game": { "kind": "alarm", "rotation_speed": "-60" } },
    { "id": 8, "parent": 7, "name": "Alarm west beam",
      "transform": { "translation": [0, -0.7, 0], "rotation": [0, 0, 0.21, 0.98] },
      "light": { "type": "spot", "color": [1, 0, 0], "intensity": 965, "range": 28,
                 "inner_angle": 16, "outer_angle": 20 } }
  ]
}
```

- **Entities** have a stable `id` (a positive integer, unique in the file), a `name` for people, an optional `parent` (another entity's id), a transform (translation, rotation as a quaternion `[x, y, z, w]` as in glTF, scale), and components. Each component is an object of its own, which the file may leave out: `model`, `light`, `fog`, `wind`, `particles`, `attractor`, `weather`, `ribbon`, `lightning`, and `game`.
- **Every component the engine draws or simulates is typed**, with the fields of its `scene::` component, in the file's units: meters, seconds, candela or lux, kelvin, and **degrees** for angles, which people read and type.
- **`game` holds what only the game understands**: a `kind` and string values, as the node names held. The engine gives it to the entity as `scene::Tags` and does not read it.
- **References to other entities are ids** (a ribbon's target, the light a storm flashes), and a reference to a part of a model is the entity and the node's name (the triangles an emitter emits on).
- **Paths are relative to the level file's directory.** Models are glTF files; effects and ribbons are their files (ADR-0053).
- **The environment** is the sky, global fog, wind and exposure that `scene::Scene` held and the game set in code.
- **Unknown keys are errors**, which name the key and the entity, so a typo is not silently a default. A newer `version` than the engine knows is an error too.
- **The engine writes the file**, with sorted keys, two-space indents and the shortest numbers that read back the same, so that a level saved twice is the same file and its diffs are readable.

### glTF models stay what they are

A model's nodes become child entities of the entity that places it, as `Instantiate` makes them. A node named `kind;key=value` still gets `Tags` from its name, so a model made in Blender can still carry doors and ladders, and a bare glTF file still loads as a level of one entity. `NodeName` stays, for the node's name as it is.

### The factory's level

`scripts/gltf_to_level.py` converts a glTF level once:

- Nodes without a mesh that only mark a place (lights, emitters, fog, wind, attractors, weather, ribbons, lightning, the sky, the player's start, zombies and fans that name their model) become level entities, at their world transform, and leave the glTF file.
- The props that move, or that a level designer moves around (the lamps with their lights, the alarms, the pickups), go to glTF files of their own under `Props/`, one per mesh, shared by every copy, and become entities that place them.
- The level's walls and floors, the doors, the ladders and the glass stay in `Factory.gltf`, with their tags.
- What the game built in code becomes data: the moon (a directional light), the sky and its yaw, the exposure and fog, the dust of the start room, the steam of the leak, and the alarms' turning beams. The flashlight and the muzzle flash stay in code: they are the player's, not the level's.

### Loading

`scene::ParseLevel` and `scene::WriteLevel` read and write the file, pure functions tested on their own. `scene::LevelModels` lists the models a level needs, which a loading screen loads on workers first. `scene::LevelInstance` then creates the entities, one at a time, so the editor can add, change and remove one without rebuilding the rest; each gets a `scene::LevelEntityId`.

## Consequences

- The game's level code only reads `Tags` and runs gameplay. Lights, fog and effects are built by the engine, so the editor draws a level as the game does.
- `sandbox.scene` takes a `.level.json` file or, as before, a glTF file. Its default becomes `Factory/Factory.level.json`.
- `Factory.gltf` is edited again by a script; the importer of the legacy level already no longer reproduces it (9.24).
- The level file has no includes, prefabs or overrides of a model's nodes: a door inside `Factory.gltf` is moved in Blender. Prefabs come with Phase 15's editor growth.

## Alternatives considered

- **glTF extras for everything**, the level remaining a glTF file: one file, Blender as the editor. The editor would have to write glTF back, keeping everything it does not understand, and the engine's data would sit in a format meant for models. Kept only for tags that ride on geometry.
- **A binary format**: faster to read, unreadable in a diff, and a level's size is a few hundred entities.
- **Overrides of a model's nodes** (Godot's and Unity's instanced scenes): the most flexible, but a second level of identity inside every model. Not needed while models are props and walls.
- **Radians in the file**, as in the components: exact, but unreadable to people who edit it.
