# ADR-0019: Audio with miniaudio

- **Status:** Accepted
- **Date:** 2026-11-01

## Context

Phase 6 needs footsteps, doors, pickups, alarms, fans and music, placed in the world. OVERVIEW prefers a mature library over writing device management and mixing. Tests and CI machines have no audio output, and a game must not refuse to start because of that.

## Decision

- **Library:** miniaudio 0.11.25 from vcpkg, with its high-level engine for mixing and spatialization. It is hidden in the `audio` module, whose public headers show no miniaudio type. OGG Vorbis goes through stb_vorbis, which already comes with `stb`.
- **Ownership:** like the renderer, `audio::AudioEngine` belongs to the application. Like physics, `audio::SoundWorld` belongs to the scene, with its clips and voices, so restarting a level also restarts its sounds. The scene receives the engine as a pointer.
- **No output:** creating the engine can fail. The application logs a warning and continues, and a `SoundWorld` without an engine is silent: clips still report missing files, and `Play` returns a null voice.
- **Tests:** an engine without a device mixes on demand with `AudioEngine::Read`, so tests check real samples on any machine.
- **Clips and voices:** a clip is decoded into memory once, or streamed for long music. A voice is one playback, with a generational handle. At most 64 voices play at once, and more are refused. Voices are released once per frame.
- **The world:** `Scene::Extract` moves `SoundEmitter` voices to their entities and the listener to the camera, from the same transforms that are drawn. There is no Doppler effect.
- **Buses:** effects and music have their own volumes, read at startup from `audio.volume`, `audio.effects` and `audio.music`.
- **Formats:** WAV for short sounds, OGG Vorbis for music. `enter_the_maze.wav` was converted once to OGG, and only the OGG is committed.

## Consequences

- Sound comes with no new binary dependency, as miniaudio and stb_vorbis compile into `audio`. `Miniaudio.cpp` is built without warnings, because it only holds third-party code.
- Volumes cannot change while the game runs. A settings menu (Phase 8) needs setters on `AudioEngine`.
- Sound passes through walls and doors: there is no occlusion. Rays from the listener to each emitter could add it later, as `PhysicsWorld::CastRay` already exists.
- Decoded clips load on the main thread. The 19 sounds of the factory take 15 ms, so a worker is not needed yet.

## Alternatives considered

- OpenAL Soft: mature, but it needs its own loaders for files and only gives a lower-level API.
- FMOD or Wwise: powerful, but proprietary, and their licences do not fit an open repository.
- The legacy OpenAL and libsndfile code: tied to the old resource system.
