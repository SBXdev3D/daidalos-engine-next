# ADR-0104: Phase 20, a better level editor

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

The proposal `docs/proposals/level-editor.md` (PR #104) lists 28 improvements to the editor: the owner's two requests (a loader while a level opens, lighting on and off), the assessment's list (`docs/ASSESSMENT.md`), and the gaps found by comparing it with Unreal Engine, Unity and Godot. On 2026-11-20, the owner approved it and asked for every item except the section "Not recommended now" (node graphs, visual scripting, terrain, level streaming, several users at once), done in a row, each step merged once checked.

## Decision

Phase 20, one PR per step, in the proposal's suggested order, items grouped where they touch the same code:

| Step | Items | What |
|---|---|---|
| 20.1 | 1, 2 | A loader while a level opens; lit, unlit, lighting only and the editor's light; the debug views in a menu |
| 20.2 | 11-14, 16 | Show flags and a game view; overlays drawn in depth; lights' reach; collision, navigation, rooms and light-count views; pausing the viewport's time |
| 20.3 | 17, 18, 20 | Hide, lock, isolate and folders; pivots, align and spread; a measuring tool |
| 20.4 | 5, 6 | Top, front and side views and a split layout; snapping to surfaces and vertices, Drop to floor |
| 20.5 | 3, 28 | Undo for every document; several documents in tabs |
| 20.6 | 4 | Simulate in the viewport, then play in the editor |
| 20.7 | 22, 23 | Map check and level statistics |
| 20.8 | 8, 27 | Context menus, search everywhere, a command palette, remappable shortcuts |
| 20.9 | 7 | Prefab overrides and nesting |
| 20.10 | 9 | An effect editor with a live preview |
| 20.11 | 10 | Geometry for blocking out rooms |
| 20.12 | 15, 19, 21 | Sound in the editor, scattering props, a camera preview |
| 20.13 | 24, 25 | Asset references and safe renames; import settings per asset |
| 20.14 | 26 | A tidier interface; the assessment and the guide brought up to date |

Each step has its ADR, unit tests for what can be tested without a window, and screenshots of the editor checked before it is merged. A step that turns out larger than its value is cut down and says what it left.

## Consequences

- The editor stays on Dear ImGui (ADR-0055); its look is reworked in 20.14 rather than replaced.
- Where an item needs the renderer (unlit views, overlays in depth, orthographic views), the change is made in the renderer behind a setting the game never uses, so the game's image and budget do not move.
