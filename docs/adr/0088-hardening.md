# ADR-0088: Phase 17, hardening

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Phase 17, agreed with the owner on 2026-11-16, hardens the engine, the demo and the editor before an assessment closes the roadmap. Step 16.6 (ADR-0087) measured the dressed factory on every preset and listed what to do. The owner asked, for the assessment, whether portal visibility is implemented: it is not, ADR-0073 having deferred it until content needed it, which the dressed factory now does.

## Decision

Phase 17 is five steps, each one PR:

1. **17.1, portal visibility.** Rooms are visibility cells in the level file. The engine finds the portals between cells where their boxes overlap, at doors and openings, and draws only what lies in the cells seen through them from the camera's cell.
2. **17.2, levels of detail.** meshoptimizer, as ADR-0081 decided once a G-buffer passed 1.5 ms. Vertex caches are ordered at import, and simplified index lists share each mesh's vertices. The mesh pass chooses one by how large the mesh is on screen. Rays keep the full mesh.
3. **17.3, the presets' costly passes.** Ultra's indirect light, four times High's, and reflections on wet floors, measured against the image they give.
4. **17.4, a bug sweep:**
   - the unit and GPU tests under AddressSanitizer;
   - the shutdown that takes seconds after the last frame;
   - settings that a player's file keeps after they are gone;
   - the gameplay numbers set in Phase 16 (ambushes, recoil), played and tuned.
5. **17.5, the assessment.** `docs/ASSESSMENT.md` judges the engine, the demo and the editor, with what to improve marked. It answers the owner's three questions: how performance can improve, how the editor compares with Unreal Engine, Unity and Godot, and which high-quality visual techniques the engine does not have.

## Consequences

- The roadmap ends with the assessment. What it recommends is a new roadmap, for the owner to decide.
