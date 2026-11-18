# ADR-0072: Saved games and checkpoints

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 13 (ADR-0041) asks for checkpoints and saves of the world's state (doors, items, triggers, AI). Its completion criterion: a saved game restores the world the player left. The sandbox could only start the factory over.

## Decision

- **A save is text** (`scene::SaveData`): a header line, then one line a value, `key = value`, the keys dotted paths (`player.health`, `entity.42.position`), the values numbers, true or false, quoted text, vectors or rotations. It reads and diffs as text, and needs no schema: what a game does not find keeps the value the level gave it. `WriteSave` writes a file whole, through a file beside it renamed over the old one, so that a crash never leaves half a save.
- **A saved game is the level made anew, then put back:** the game loads the level as it always does, then applies the save to it. Entities are found by what does not change while playing: a level entity by its id (`LevelEntityId`, ADR-0054), anything else by a `SaveKey` the game gives it when the level is made, before anything is taken or destroyed (the sandbox uses the node's name and its count: `door;id=-2#0`).
- **The engine keeps its part** (`SaveScene`, `LoadScene`): where each level or keyed entity is, which lights are on, what rays see, which triggers have fired, how wet the wet places are, and the power circuits. The game keeps its own under its own keys.
- **The sandbox keeps** the player (where they stand and look, health, armor, weapons and ammunition, flashlight and battery, kills), the pickups not taken, the doors (open, locked, how far up), and the zombies (where, which way, health, dead or not). F6 saves, F9 starts the factory over from that save or else from the last checkpoint, `sandbox.continue` starts from the last checkpoint, and a checkpoint is a sequence's `checkpoint` event (ADR-0071): the rain room's first steps save one.

Tested: values come back as they went, and what is not a save is an error naming its line (`SaveGameTest`); a world changed (an entity moved, a keyed one moved, a light off, a trigger fired, a circuit cut), saved, and loaded into one made anew from the same level is the world that was saved (`ASavedGameRestoresTheWorldItLeft`). In the factory, stepping into the rain room writes a checkpoint of 11.7 KB, and `sandbox.continue` starts the game there.

## Consequences

- Zombies come back where they were, but waiting: what they were doing (chasing, searching) is not kept, so a load gives the player a moment.
- Particles, sounds playing and sequences halfway through are not saved: they start over from what the world is.
- A save made with one version of a level and loaded into another puts back what it can find by its keys and ignores the rest.

## Alternatives considered

- **Serializing the whole ECS:** nothing to choose, but everything to version, including what is rebuilt anyway (renderer handles, physics bodies); the level made anew rebuilds those.
- **A binary format:** smaller, but not readable when a save goes wrong; a factory save is 12 KB of text.
