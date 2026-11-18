# ADR-0071: Triggers, sequences and cinematics

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 13 (ADR-0041) asks for events and sequences (timelines of light, sound, camera and entity cues, started by triggers) and for cinematics (a sequencer for cutscenes and scares, camera paths, focus pulls, handheld shake), with a scripting language only when someone who does not write C++ authors content. Its completion criterion: a scare can be authored without engine changes.

## Decision

- **Sequences are data:** a `.sequence.json` file is a list of cues, each a time in seconds and one kind (`scene::ParseSequence`, unknown keys and kinds are errors that name them):
  - `power` cuts or restores a power circuit (ADR-0057), `light` turns a light on or off, `visible` shows or hides an entity from every ray (ADR-0059);
  - `sound` plays a clip, where an entity is or in the head, with ducking (ADR-0067); `particles` spawns an effect where an entity is; `move` slides an entity to a position;
  - `shake` shakes the camera by hand; `camera` takes it along keys (position, target, aperture and focus, eased between keys: camera paths and focus pulls), then hands it back with a cut; `mix` fades the mix to a snapshot;
  - `event` names something only the game knows how to do, which its code takes (`Scene::TakeEvents`).
  Entities are named as the level names them; paths are relative to the asset root.
- **The scene plays them** (`Scene::PlaySequence`, `PlaySequenceFile`, reading each file once), several at once, each cue in time order, on the scene's clock (paused with it).
- **Triggers start them:** a level entity's `trigger` component (a box: the entity's unit cube, as fog and wet volumes) plays its sequence when something with `TriggerActivator` (the sandbox's player) walks in, once or each time. The editor places triggers (Create > Trigger) and edits them in the inspector.
- **No scripting language:** cues cover what scares are made of, and events reach game code for the rest; Lua comes when someone who does not write C++ needs more.
- **The factory's first scare is data only:** the first steps into the rain room set off a moan from its far dark corner, a moment of muffled sound, a shake, sparks from the ceiling and a stinger (`assets/Factory/Sequences/rain_room_scare.sequence.json`, and one trigger entity in the level).

Tested: every kind of cue parses, in time order, and mistakes name what is wrong; the camera eases between its keys (`SequenceTest`); a trigger's level entry survives a round trip; something walking into a trigger, out and in again plays its sequence once: its event is taken and its crate moved (`ATriggerPlaysItsSequenceOnce`).

## Consequences

- A sequence cannot wait for something (a door to open, the player to look): it is a timeline. Conditions go in game code, which events reach, or into triggers placed where the condition is met.
- Camera cues take the camera from whatever has it, the player included; the game decides whether the player can move meanwhile.
- The editor has no timeline view: sequences are edited as text, beside the level.

## Alternatives considered

- **Lua scripts for scares:** any logic, but a language, its runtime and its bindings to maintain, for what a list of timed cues already does.
- **Cues as level entities** (a sequence a parent with timed children): editable in the editor, but a timeline is clearer read in order, and the inspector stays for what is placed in space.
