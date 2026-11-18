# ADR-0084: Pacing, scares and a way out

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Step 16.3 of the showcase (ADR-0082). The factory had one scripted scare, in the rain room. It ended only when every zombie was dead, and its end screen gave the time alone. `docs/DEMO.md` asks for:
- dread between the fights: lights that die, sounds behind walls, ambushes;
- a clear exit;
- an end-of-level summary.

## Decision

- **Rooms have power circuits.** The dressing script (ADR-0083) now places each lamp as its model and its light, not as a prefab, because a prefab has no per-copy overrides. Each light is on its room's circuit: `loading_floor`, `workshop`, `yard`, `pumps` and so on. One lamp in four buzzes or is failing.
- **Two new events the game takes from sequences** (ADR-0071):
  - `ambush` makes a noise where the player is, heard 30 m away along the navigation mesh (ADR-0070), so the zombies around come.
  - `exit` ends the level as the death of the last zombie does.
- **Five scares along the route,** staged by `scripts/stage_showcase_scares.py` into sequence files and trigger boxes under "Scares: showcase", which a run replaces:

  | Where | What happens |
  |---|---|
  | Entering the loading floor | Its power dies, something groans by the crane, the camera shakes, then the lights come back stuttering. A checkpoint. |
  | Entering the workshop | A groan from behind the wall, its lights cut, an ambush. |
  | Out into the yard | The alarm, the floodlights die, an ambush. |
  | Down to the pumps | Their lights go out for good. The mix muffles, then a moan; the flashlight is all there is. A checkpoint. |
  | The far end of the pumps | A shaft going up, under a floodlight: the way out. |

  Each comes with a subtitle in English and French and captions for its sounds (ADR-0075).
- **The summary:** "YOU GOT OUT" or "FACTORY CLEARED", with the time, the zombies killed out of how many, and the items found out of how many. Items found are saved with the game.
- A test parses every sequence file under `assets/`.

## Consequences

- The level can be finished without killing every zombie: speed against thoroughness, which the summary shows.
- An ambush brings every zombie within 30 m of walking, which may be most of the hall's. The numbers are tuned in play, in Phase 17.
- Rerunning the dressing script changes which lamps buzz (it is seeded) and the circuits' members, not their names, so the scares keep working.

## Alternatives considered

- **Per-instance overrides of prefabs**, to keep the lamps prefabs with a circuit each: the general answer, refused in ADR-0078 until needed. Two hundred placed lamps written out by a script do not need it.
- **Ambushes as zombies spawned out of sight:** more control, and zombies that were never in the level, which the kill count and saves would have to know about. Waking the ones already there keeps the count true.
