# ADR-0109: Undo for every document; documents in tabs

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.5 of Phase 20 (ADR-0104): proposal items 3 and 28.

- **Two editors had no undo.** Material edits (ADR-0079) and sequence edits had none: only Revert, which reads the file again. A wrong slider move on a material meant reverting every change made to that model since its last save.
- **One document at a time.** Opening a prefab from the inspector replaced the level, so editing a lamp prefab and seeing it placed in the level meant opening the files in turns.

## Decision

- **One history for everything the editor edits.**
  - `UndoHistory<State>` is the history of ADR-0055 made a template. The editor's state is a `Document`: the level, the values of every material edited since the level opened (by model and index), and the sequence open in its panel (its file and its cues).
  - Steps are still whole copies. A material's values are a few hundred bytes and a sequence a few kilobytes, beside the level's 30 KB.
- **The panels report; the editor records.**
  - The material panel queues each change of a material's values, a field's or Revert's (`MaterialPanel::TakeEdits`). For each one, the editor opens a step with the state before it, then keeps the new values.
  - The sequence panel is compared each frame with the sequence last seen of the same file; a difference opens a step.
  - Steps close as level edits do, when the mouse lets go, so a slider drag is one step.
  - Undo and redo give values back through `MaterialPanel::Restore`, which keeps track of whether the material still differs from its file, and `SequencePanel::SetSequence`. A sequence of another file is opened again first.
  - A material first edited after a state was saved in the history is not in that state; the editor keeps what each had before its first edit and gives that back.
- **A History panel** lists the steps that can be undone. A click goes back to just after one; "(as opened)" goes back to the start.
- **Documents in tabs.**
  - Over the view, a row of tabs shows each open level or prefab, with `*` when it has changes.
  - File > Open in a new tab (Ctrl+Shift+O, or the Open dialog's checkbox) opens beside what is open, and the inspector's "Open the prefab" now opens it in a tab. Ctrl+Tab goes to the next tab, Ctrl+W closes one, asking first if it has changes.
  - Each tab keeps its own level, scene, history, selection, camera, panes and isolation. The tabs not shown keep their scene alive, so switching is instant.
  - `Application::ExchangeScene` puts a scene in place at once and gives the previous one back, alive.
  - Quitting with changes in a tab not shown switches to it and asks.

## Consequences

- A tab not shown keeps its models on the GPU, as its scene does. A prefab's tab is small; two large levels side by side cost twice the memory.
- The material and sequence panels stay one each, shared by the tabs. Materials are keyed by their model, which belongs to one tab's scene.
- Autosave covers the shown tab.

## Alternatives considered

- **Commands per field for materials and sequences:** smaller steps, but each field of each editor would need its command, and the next field added would need one too. Whole states cannot miss a field.
- **A history per panel:** Ctrl+Z would depend on which panel had focus. One history in time order is what other editors do.
- **Tabs that reload their level when shown:** less memory, but seconds per switch for the factory, and the scene's state (a preview, particles) lost each time.
