# ADR-0118: A tidier interface; Phase 20 done

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.14 of Phase 20 (ADR-0104): proposal item 26, and the end of the phase. The editor drew Dear ImGui's own look: its pixel font, its blue widgets, words for tools. The assessment called it "a developer's look" (`docs/ASSESSMENT.md`).

## Decision

- **A dark theme with one warm accent**, the amber the editor already uses for the selection and the live pane. It brings rounder frames, more room between items and inside frames, quieter borders, and tabs whose selected one has an amber line.
- **The system's interface font**: Segoe UI on Windows, DejaVu Sans on Linux, at 16 pixels, or any `.ttf` named by `editor.font` and sized by `editor.font_size`. Without one, Dear ImGui's own font stays. The debug overlay gains `DebugUi::UseFont`, which adds the font and builds its atlas again on the GPU.
- **Icons for the toolbar's tools**, drawn with Dear ImGui's shapes rather than an icon font, so no file or licence is added: move, rotate, scale, local or world axes, snapping, paint, measure, play in the editor, simulate. Each has a tooltip naming its keys, read from the keymap (ADR-0112). The active tool is lit in amber.
- The layout stays saved beside the editor (`editor_layout.ini`); View > Reset the layout puts it back. New panels join the old layouts, as they have since step 20.2.
- **Phase 20 is done.** Every item of `docs/proposals/level-editor.md` but its "Not recommended now" section is in (ADR-0105 to ADR-0118). The assessment's editor section is brought up to date.

## Consequences

- Text is wider than Dear ImGui's pixel font: long names in the hierarchy are cut sooner.
- The game's menus are drawn by its own interface (ADR-0027), not this theme.

## Alternatives considered

- **An icon font** (Font Awesome, Material Symbols): richer icons, but a font file and its licence to carry, merged into the atlas.
- **Light theme:** editors for dark games are used in dark rooms; the viewport's image reads better beside dark panels.
