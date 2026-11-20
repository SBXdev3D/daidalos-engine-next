# ADR-0112: Commands, search everywhere, context menus and remappable keys

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.8 of Phase 20 (ADR-0104): proposal items 8 and 27. Shortcuts were a long `if` chain, fixed in the code and listed by hand in a help table that drifted from it. Nothing in the view or the hierarchy answered a right click, and an entity, an asset or a setting could only be found by browsing for it.

## Decision

- **Commands.** Every action with a key is a `Command`: an id (`file.save`), a name, default keys, what it does, and when it can run. That is about 60 commands, the camera bookmarks included. The keys, the menus' key hints, the Shortcuts window and the palette all read the same list, so they cannot disagree. A frame's key press runs the first command whose keys match exactly, modifiers included.
- **Remappable keys.** F1's Shortcuts window lists every command with its keys and finds them as typed. "Change" waits for the next keys pressed, Backspace takes them away, and "Reset" goes back to the default. Keys used twice show in red. What differs from the defaults is written to `editor_keys.txt` beside the editor, one `id = keys` per line.
- **Search everything (Ctrl+P),** as editors' command palettes do. One field searches commands, the level's entities, the assets under `assets/`, and the settings, with a fuzzy match: letters in order, better together, at the start of words, or as one piece. Up, Down and Enter choose:
  - a command runs;
  - an entity is selected and framed;
  - an asset is placed or opened;
  - a setting asks for its new value.
- **Context menus.**
  - A right click in the view that does not move (under 4 pixels and 0.35 s) selects what is under it and opens what can be done to the selection: frame, rename (F2), duplicate, copy, cut, paste, delete, hide, isolate, lock, drop to the floor, folder, prefab, open or unpack the prefab, edit the trigger's sequence. It also offers Paste here, Create here (the Create menu at the surface clicked) and Measure from here. A right drag still flies or pans.
  - Hierarchy rows have the same menu.
  - Folder rows offer to select, hide, lock or empty what they hold.

## Consequences

- The help table of ADR-0055 is gone: the Shortcuts window is the list, and is always true.
- Commands that share a key with a menu's tool, such as W while flying, are not run while flying or typing.
- The asset list for search is walked once a minute at most, leaving out texture sources.

## Alternatives considered

- **Keys as settings (cvars), as the game's bindings are (ADR-0076):** settings hold one value each and are the game's; the editor's keys are a small file of its own, and only what differs is kept.
- **A palette of commands only:** the proposal asked for search everywhere; one field for all four is what editors' palettes have converged on.
