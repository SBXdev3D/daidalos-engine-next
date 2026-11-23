# ADR-0065: Steam Audio for HRTF, occlusion and transmission

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 12 (ADR-0041) asks for Steam Audio (Apache 2.0) behind the `audio` module: HRTF, occlusion and transmission through walls and doors, reverb from traced paths, propagation around corners. Until now miniaudio panned each sound by its direction and faded it by distance: a zombie behind a wall sounded as if the wall were not there. Steam Audio 4.8.1 has a vcpkg port, built from source as a static library.

This step (12.1) brings the direct path: HRTF, occlusion and transmission. Reflections and pathing follow in 12.2.

## Decision

- **Steam Audio comes through vcpkg** (`steam-audio`, linked privately by `audio`; its package needs flatbuffers, pffft and mysofa found first). Nothing outside `engine/audio/src` sees its headers.
- **The engine holds Steam Audio's context and default HRTF** (`AudioConfig::steamAudio`, on by default). If it cannot start, sounds fall back to miniaudio's panning, as before.
- **A sound in the world plays into its own miniaudio node** (`SpatialNode`): the voice's `ma_sound` is not spatialized by miniaudio; the node mixes it to mono and runs Steam Audio's direct effect (distance, air absorption, occlusion, frequency-dependent transmission), then its binaural effect, in blocks of 256 samples, which is its latency (5.3 ms at 48 kHz). The node outputs to the voice's bus.
- **The world's acoustic scene** is Steam Audio's own ray tracer over meshes the game gives: `SoundWorld::AddAcousticMesh` for the level's walls and floors, in world space, and `AddMovableAcousticMesh` / `MoveAcousticMesh` for doors, as instances of their own sub-scene. Each mesh has an `AcousticMaterial` (absorption, scattering and transmission in three bands); `AcousticMaterialOf` maps the surfaces the game already names (concrete, metal, wood, glass, tile, cloth...) to Steam Audio's measured presets.
- **Each frame, `SoundWorld::Update` simulates every voice's direct path** on the game's thread: volumetric occlusion (16 rays over a ball of 30 cm), transmission through up to two surfaces, air absorption. The result goes to each voice's node behind a lock the audio thread only tries. A voice that moves between updates takes its new direction and distance at once, with the occlusion last found. Distance attenuation keeps the old model (full within the minimum distance, inverse beyond), so that games keep their tuning; past the maximum, it was constant, and is now faded out by twice the maximum (2026-11-23: the effects gallery's twelve rooms were each heard in all the others).
- **The sandbox gives the factory's collision geometry to the acoustic scene**, each part with its surface, and each door as a movable mesh that follows the door.

Measured: a 440 Hz tone 4 m away behind a concrete wall drops to 0.9% of its level (`AWallMuffles`); behind a closed metal door, 12% of its level (low frequencies pass), back to full once the door swings away (`ASoundBehindADoorIsMuffledUntilItOpens`). In the factory, the simulation takes 0.09 ms per frame with 5 voices (`renderer.log_gpu_times` also logs it).

## Consequences

- The HRTF tells the ears apart less than panning did at low frequencies: a 440 Hz tone at the side is 1.8 times louder in the near ear rather than more than 2; the tests were changed to that.
- Voices in the world have 5.3 ms more latency.
- Occlusion does not yet find the way around: a sound behind a wall with an open door beside it is muffled as if the door were shut, until pathing (12.2).
- Levels made in the editor give their geometry only through the game's code, as the sandbox does; the level file does not hold acoustic geometry yet.

## Alternatives considered

- **Our own occlusion with Jolt's ray casts** and a filter per voice: no new dependency, but no HRTF, no measured materials, and reverb and pathing would all be ours to write.
- **The prebuilt Steam Audio SDK, vendored:** a binary in the repository, and a DLL to ship; the vcpkg port builds it from source, statically.
