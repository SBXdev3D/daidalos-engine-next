# ADR-0086: The opening, and fixed showcase shots

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Step 16.5 of the showcase (ADR-0082). The legacy demo opened with a camera flight over its level before the player took control (`docs/DEMO.md`), and the remake had none. The showcase also needs pictures from the same places each time, so that a change shows as a difference.

## Decision

- **`sandbox.opening`** names a sequence that plays once, when a new game starts from the main menu. It defaults to `Factory/Sequences/opening.sequence.json`; empty means none. Starting with `sandbox.menu false`, as tests and screenshots do, skips it.
- **The opening is data:** a camera cue (ADR-0071) of six keys over 19 seconds. It flies high over the loading floor past the crane, with a shallow depth of field, then down the corridor into the start room, and ends at the player's eyes. A muffled mix lifts as it arrives, with a moan halfway. It is edited in the sequence panel like any scare (ADR-0079).
- **While a camera cue has the camera,** the player does not move, look or shoot, and the HUD is hidden. Subtitles still show.
- **Skipping:** the menu, jump or use key skips it (`Scene::StopCinematic`). A shot does not, so a habitual click does not skip the flight.
- **`scripts/showcase_shots.ps1`** starts the sandbox at eight fixed places with the flashlight on, and saves what the window shows to `build/showcase/`. `-Bright` lifts the darkness to check placement.

## Consequences

- The flight goes through the level as it is dressed now. If a room changes, its keys may need moving in the sequence panel, where the path is drawn in the view.
- The shots are Windows only, as they drive the window through user32.

## Alternatives considered

- **A camera path recorded by flying the editor's camera:** natural to author, and a recorder to build. Keys taken "from the view" in the sequence panel do most of it already.
