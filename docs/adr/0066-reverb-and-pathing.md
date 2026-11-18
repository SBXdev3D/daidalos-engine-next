# ADR-0066: Reverb and pathing

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 12 (ADR-0041) asks for reverb from traced paths and for propagation around corners and through open doors, on worker threads over several frames. After step 12.1 (ADR-0065), a sound behind a wall was muffled whatever way there was around it, and every room sounded the same, dry.

## Decision

- **A worker thread per world** (`steam::Scene`) runs a round every 50 ms: the reverb, then the paths. The game's thread keeps the direct path (12.1). The simulator is changed (geometry committed, sources added and removed, doors moved) only while the worker does not run it: the game's thread tries the lock, and when the worker has it, what it would do waits (doors' moves and sources are queued), and voices take only their new direction and distance that frame.
- **Reverb is the listener's room** ("listener-centric" in Steam Audio's terms): reflections are traced from a source at the listener (1024 rays, 8 bounces, up to 2 s), and the parametric reverb they give plays through one reverb node on the effects bus. Every spatial voice sends to it the sound it plays, as loud as the square root of its distance attenuation: far sounds reverberate more than they come direct, as they do in a room. Without geometry there are no reflections and no reverb.
- **Pathing** goes from probes, on the worker: `SoundWorld::StartPathing(spacing)`, once the static geometry is in, places probes 1.5 m above every floor triangle on a grid a spacing apart (Steam Audio's own floor placement looks down from above the whole level, and in the factory found only its roofs: 29 probes against 829), and bakes the paths between them (within 30 m, paths up to 60 m). Then each round finds, for each voice, the way from it to the listener through probes that see each other, checked against the doors as they are now, and gives its node first-order Ambisonics of where the sound arrives from. The node plays that path through the HRTF as much as the direct path is occluded: a sound in plain sight comes only direct, one behind a wall comes from the doorway.
- **The factory starts pathing** once its walls and doors are in: 829 probes 3 m apart, baked in 0.3 s.

Measured: a tone behind a wall with an opening to its side is 7.9 times louder once paths are baked (`SoundFindsTheWayAroundAWall`); a closed concrete room rings on after the sound stops (`ARoomRingsAfterTheSoundStops`). In the factory, the game's thread spends 0.08 ms per frame on sound with 5 voices, and the worker 6.9 ms per round, every 50 ms, about 14% of one core.

Steam Audio 4.8.1's path baker crashes when it is given no progress callback; the engine gives it an empty one.

## Consequences

- Every sound in the world reverberates as if it were in the listener's room: a sound in a hall heard from a cupboard sounds like the cupboard.
- A new or moved door reaches the reverb and the paths at the worker's next round, up to 50 ms later.
- Probes stand on floors only: a sound in a ventilation duct has no path through it.
- The paths are baked at each start of the level, which costs a fraction of a second in the factory; bigger levels may want them baked offline and saved with the level.

## Alternatives considered

- **Convolution reverb per source:** each sound with its own reflections, from where it is, but a convolution per voice; the parametric reverb around the listener costs one effect.
- **Portals between rooms, by hand:** cheaper and exact where placed, but a level designer's work for every door, which probes and visibility do by themselves.
