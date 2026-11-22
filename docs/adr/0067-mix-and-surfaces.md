# ADR-0067: Buses, snapshots, ducking, and sounds by surface

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 12 (ADR-0041) asks for buses, snapshots and ducking on miniaudio's sound groups, and for acoustic properties on materials next to the visual ones, with footsteps and impacts per surface. The mix had two buses at the player's volumes and nothing else; every step sounded the same, and shots landed in silence.

## Decision

- **Four buses:** effects, music, ambience and interface (`audio::Bus`). Effects and ambience are the world's sounds; music and the interface are not in the world. The player's effects volume sets effects, ambience and the interface; their music volume, music.
- **Snapshots** (`audio::MixSnapshot`): each bus's share of its volume and a low-pass cutoff on the world's sounds. `SoundWorld::SetSnapshot` fades to one over the seconds it is given, buses linearly and the cutoff in octaves; `Update` moves the mix with its own clock. The world's buses go through one low-pass node only while a snapshot muffles them (below 18 kHz), so that the filter adds nothing, not even a tail, the rest of the time.
- **Ducking:** a voice's `PlayOptions::ducking` lowers music and ambience by that share while it plays, down in about an eighth of a second, back up in about two thirds.
- **Materials sound like what they are made of:** the surface a glTF material names in its extras, which already chooses its impact particles and its acoustic material (ADR-0065), also chooses its footsteps and its impact sounds.
- **The sounds are made by a script**, `scripts/make_surface_sounds.py`, from noise, filters and decaying partials with a fixed seed: four footsteps each on concrete, metal, wood, tile and water, three impacts each on concrete, metal, wood, tile, glass and flesh, 1.8 MB in all, ours under the project's licence (the assets are CC0 or ours, ADR-0041).
- **The sandbox** finds what is underfoot by a ray down, or water where a wet area's puddles stand (ADR-0061), and plays its step over the old step's thud; a shot plays one impact where its first pellet lands; looped sounds play on the ambience bus; a wound muffles the world for a moment and death muffles it for good.

Tested: a 100 Hz snapshot takes a 440 Hz tone on the effects bus to 5% while the same tone keeps playing as music (`ASnapshotMufflesTheWorldButNotTheMusic`); a ducking voice brings the music below half (`AStingerDucksTheMusic`).

## Consequences

- Synthesized steps and impacts are plain next to recorded ones; recorded CC0 sounds can replace them file for file.
- Snapshots belong to a sound world: a game that keeps two worlds at once would have them fight over the engine's buses.
- The interface bus has no sounds yet.

## Alternatives considered

- **A low-pass node per voice:** muffles each sound by its own cause, but a filter per voice to drive; one filter on the world's buses is what a snapshot needs.
- **Recorded sounds:** better sounding, but each needs a licence checked; the script's sounds need none.

## Amendment (2026-11-21): a puddle that sounds like water

The owner found the step in a puddle unrealistic: it was one burst of filtered noise and two to four sine tones. It is now built as water sounds are (van den Doel, "Physically based models for liquid sounds", 2005): the sole's slap, a slosh, 25 to 40 air bubbles that each ring at the Minnaert frequency of their radius (0.8 to 6 mm, most small), damped and rising in pitch as they near the surface, a few drops falling back, and a smaller splash with its own bubbles as the foot lifts. `scripts/make_surface_sounds.py` gives the water step its own dice, so that the impacts made after it stay the same files.
