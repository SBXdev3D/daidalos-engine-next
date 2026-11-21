# ADR-0068: A score that follows the danger, stingers, and sounds that follow lights

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 12 (ADR-0041) ends with dynamic music in layers driven by a tension value, stingers for scares, and sources that follow a light's flicker. The factory played the old demo's track on a loop, whatever happened, and its flickering tubes were silent.

## Decision

- **Layers:** `SoundWorld::PlayLayers` loops every clip of a score on the music bus, all started on the same sample of the mixer's clock (`PlayOptions::delay`). The first layer always plays; each next one fades in as the tension (`SetTension`, 0 to 1) passes its share (with n layers, layer i is full at tension (i + 0.5) / n), in at 0.7 per second and out at 0.3, so that fear comes faster than it goes.
- **The score is made by a script**, `scripts/make_music.py`, as 16-second loops whose every frequency fits a whole number of cycles, so that they loop without a click: a low drone on D and A, a heartbeat at 60 beats a minute, a dissonant cluster of detuned saws, and a high tremolo of D against E-flat. Two stingers (a boom, a burst, a shriek) and a tube's hum come from the same script. 3.4 MB in all, ours under the project's licence.
- **Stingers** play on the interface bus, which no snapshot muffles and no ducking lowers, and duck music and ambience by 60% while they play (ADR-0067).
- **Sources follow lights:** a `scene::SoundEmitter` that `followsLight` plays as loud as its entity's light shines (`LightState`, ADR-0057), so a buzzing tube's hum stutters with it and stops when it fails.
- **In the factory:** the tension is the closest danger: a zombie hunting the player gives 0.55, up to 1 as it closes in from 20 m; one waiting within 15 m, up to 0.3. The first zombie to start the hunt plays a stinger, at most one every 25 seconds. Every light that buzzes or fails hums. The old track stays a setting away (`sandbox.classic_music`).

Tested: three layers at tension 0 play only the first, at 1 all three, back at 0.4 the second at 70% and the third silent (`LayersComeInAsTensionRises`); a hum at 0.5 under a light at 20% plays at 0.1, and at 0 when the light is out (`AHumDiesWithItsLight`).

## Consequences

- Phase 12's sound is done: HRTF, occlusion and transmission (ADR-0065), reverb and paths (ADR-0066), the mix and surfaces (ADR-0067), the score and the lamps (ADR-0068).
- The synthesized score is a placeholder for a composed one: a composer's stems drop in as layers.
- Tension comes from the zombies only; darkness, the player's health or a scripted scare could add to it (Phase 13).

## Alternatives considered

- **Crossfading whole tracks by intensity:** simpler to compose for, but the change between tracks is heard; layers that play all along change without a seam.
- **Horizontal resequencing** (segments chosen at bar lines): more musical, but needs a score written for it.

## Amendment (2026-11-21): a drone that is heard

On ordinary speakers the base layer sounded like loud noise and no music: its notes were D1 and A1 (36 and 55 Hz), which few speakers play, under filtered noise three times as loud; and its loop clicked, the noise's filter starting from a raw sample instead of the state the loop ends in. The drone is now a dark open chord an octave up (D2, A2, D3, F3, each with a slowly beating twin and overtones that swell once a loop) over a quieter wind and a D1 beneath; the filter starts from the loop's end. The notes are 2.8 times the wind's level (1.2 before), 41% of their energy at or above 100 Hz (none before), and the seam is within an ordinary step. It is also written 8 dB below the other layers (peak 0.24 against 0.6): it plays all the time, so calm is quiet and the danger layers stand out.

## Amendment (2026-11-21, later): the old track under the score

Still too loud for the owner, who also asked for the old demo's music at a low volume. The drone lost its D1 (a rumble on headphones) and went 6 dB lower again (peak 0.12, about -31 dBFS RMS, from -16 at first), and the old demo's track (`enter_the_maze.ogg`) now loops all through play at a third of its old volume (0.1 against 0.35), under the score. `sandbox.classic_music` plays it alone at full volume, as before. After the tour (ADR-0120), the track comes down to that level over three seconds instead of stopping.
