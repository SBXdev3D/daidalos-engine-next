# ADR-0114: An effect editor with a live preview

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.10 of Phase 20 (ADR-0104): proposal item 9. Particle effects and ribbons (ADR-0053) are the files edited most after levels. They were edited as JSON in a text editor while the game reloaded them. Their curves are lists of `[time, value]` keys, read as numbers rather than seen as curves.

## Decision

- **`scene::EffectDocument`** reads an effect or ribbon file into the scene's JSON tree. It gives typed access by key path (`emitter.rate`, `light.intensity`): numbers, flags, texts, ranges, vectors, curves, and color keys. Keys it is not asked about are kept, the comment included. It writes the file back with its keys sorted, as the editor writes every file.
- **The Effect panel** shows a particle effect as fields in sections: look, life, motion, where particles start, surfaces, emitter, and light. A ribbon gets its own fields.
  - Curves over life are drawn as curves: drag a key, double-click to add one, right-click a key to take it away. Color over life is a gradient with keys under it, each with its color and its time.
  - A change is written to the file at once, if the game's own reader takes it (`ParseParticleEffect`, `ParseRibbonEffect`). The game's effects reload when their file changes (ADR-0053), so every emitter of the level shows it: the live preview is the level itself. A value the reader refuses is not written, and its complaint shows.
  - Opening a file writes nothing. Revert puts back the file as it was opened.
  - Edits are steps of the editor's history (ADR-0109): the document keeps the effect's path and text.
  - It opens from the Assets panel's new right-click menu (Edit, also Place and Copy its path), from an emitter's "Edit its effect" in the inspector, or with `+editor.effect <path>`.
- **An editor smoke test.** `editor.smoke` starts the editor on the factory with an effect open and the validation layers on, and quits after 200 frames (`editor.frames`). Like the sandbox's, a crash or a logged error fails it. It was added after step 20.8 merged a crash at start that no test caught: `Command` defaults had been given menu key lookups, which are null before the commands exist. That is fixed here.

## Consequences

- Effect files saved by the panel are rewritten with sorted keys and two-space indents, as levels and glTF materials are.
- Bursts and sub-emitters (`onDeath`, `onHit`) keep their values but have no fields yet; they are edited in the file.

## Alternatives considered

- **A preview viewport of its own,** as Unreal's Niagara has: the renderer draws one view a frame (ADR-0108). The level is a better preview for this game's effects, which are made for its rooms and lights.
- **A writer from `ParticleEffectFile`:** every field written, but the comment and the author's choice of a single number over a range would be lost.
