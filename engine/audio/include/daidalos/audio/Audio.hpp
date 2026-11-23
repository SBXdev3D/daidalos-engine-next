// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/core/Result.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace daidalos::audio {

struct ClipTag;
struct VoiceTag;
/// A sound loaded by a SoundWorld.
using ClipHandle = core::Handle<ClipTag>;
/// One playback of a clip.
using VoiceHandle = core::Handle<VoiceTag>;

/// Each bus has its own volume (ADR-0067). Effects and ambience are the world's sounds, which snapshots can
/// muffle; music and the interface are not in the world.
enum class Bus { Effects, Music, Ambience, Interface };
inline constexpr std::size_t kBusCount = 4;

/// A state of the mix (ADR-0067): each bus's share of its volume, and how muffled the world's sounds are, as
/// a low-pass filter's cutoff in hertz (20 kHz or more: none). A game sets one for a menu, for being hurt,
/// for a scare, and the mix fades to it.
struct MixSnapshot {
    std::array<float, kBusCount> buses{1.0f, 1.0f, 1.0f, 1.0f};
    float lowpass = 20000.0f;

    bool operator==(const MixSnapshot&) const = default;
};

struct AudioConfig {
    /// False mixes on demand with AudioEngine::Read and plays nothing, for tests.
    bool useDevice = true;
    /// Steam Audio (ADR-0065): sounds in the world are heard through a head-related transfer function, and
    /// through the walls between them and the listener. Off, or if it cannot start, miniaudio pans them.
    bool steamAudio = true;
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
    /// Multiplies everything, from 0 to 1.
    float volume = 1.0f;
    float effectsVolume = 1.0f;
    float musicVolume = 1.0f;
};

/// The output and the mixer (ADR-0019). The application owns one, which must outlive every SoundWorld.
class AudioEngine {
public:
    /// Fails if the machine has no usable output. A game then runs silently: see SoundWorld.
    [[nodiscard]] static core::Result<AudioEngine> Create(const AudioConfig& config);

    AudioEngine(AudioEngine&& other) noexcept;
    AudioEngine& operator=(AudioEngine&& other) noexcept;
    ~AudioEngine();

    /// Mixes the next samples, interleaved by channel. Only without a device.
    void Read(std::span<float> samples);

    /// The overall volume and those of the buses, from 0 to 1, as AudioConfig sets them at the start.
    void SetVolumes(float volume, float effectsVolume, float musicVolume);

    struct Impl;
    [[nodiscard]] Impl& Internals() { return *m_impl; }

private:
    explicit AudioEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

struct ClipOptions {
    /// Read from the file while playing, for long music, instead of decoded into memory at once.
    bool stream = false;
};

struct PlayOptions {
    /// Where the sound is in the world. Without one, it plays at the same level in both ears.
    std::optional<glm::vec3> position;
    float volume = 1.0f;
    bool loop = false;
    Bus bus = Bus::Effects;
    /// Meters. Closer than the minimum, the sound is at full volume; it stops fading at the maximum.
    float minDistance = 1.5f;
    float maxDistance = 35.0f;
    /// While it plays, music and ambience drop by this share, so that it is heard over them (ADR-0067): a
    /// stinger, a voice, a scream.
    float ducking = 0.0f;
    /// Seconds before it starts, counted on the mixer's clock: voices played with the same delay in one frame
    /// start on the same sample, as the layers of a score must.
    float delay = 0.0f;
};

/// How a surface treats sound (ADR-0065), in three bands, low (around 400 Hz), middle (2.5 kHz) and high
/// (15 kHz): the share of the sound it absorbs where sound reflects off it, how much of what it reflects it
/// scatters, and the share it lets through.
struct AcousticMaterial {
    std::array<float, 3> absorption{0.10f, 0.20f, 0.30f};
    float scattering = 0.05f;
    std::array<float, 3> transmission{0.100f, 0.050f, 0.030f};

    bool operator==(const AcousticMaterial&) const = default;
};

/// How far the sounds' paths around obstacles are (ADR-0066).
enum class PathingState : std::uint8_t {
    /// Not asked for: sounds behind a wall are muffled, whatever way around there is.
    None,
    /// Probes placed and paths baking, on a worker thread.
    Baking,
    /// Sounds go around obstacles, through open doors.
    Ready,
    /// The geometry has no floor to place probes on.
    Failed,
};

/// The material of a surface by its name, as glTF extras {"surface": ...} give it (concrete, metal, wood,
/// glass, tile, cloth, flesh, stone, plaster, brick, gravel), from Steam Audio's measured presets; anything
/// else is generic.
[[nodiscard]] AcousticMaterial AcousticMaterialOf(std::string_view surface);

/// The sounds of a scene: its clips, what is playing, and who is listening (ADR-0019). Destroying it
/// stops everything, so restarting a level also restarts its sounds. Without an engine it is silent:
/// clips still load and report missing or invalid files, and Play does nothing.
class SoundWorld {
public:
    /// Clips are found under the root. The engine, if any, must outlive this world.
    SoundWorld(AudioEngine* engine, std::filesystem::path root);
    SoundWorld(SoundWorld&&) noexcept;
    SoundWorld& operator=(SoundWorld&&) noexcept;
    ~SoundWorld();

    /// Returns the clip, loading it the first time. A missing or invalid file is an error that names it.
    [[nodiscard]] core::Result<ClipHandle> Load(std::string_view path, const ClipOptions& options = {});
    [[nodiscard]] std::size_t ClipCount() const;

    /// Returns a null voice if the clip is null, when silent, when paused, or when too many sounds are
    /// playing.
    VoiceHandle Play(ClipHandle clip, const PlayOptions& options = {});
    /// A voice that ended, or a null one, is ignored.
    void Stop(VoiceHandle voice);
    void SetPosition(VoiceHandle voice, const glm::vec3& position);
    /// The voice's own volume, as PlayOptions::volume set it.
    void SetVolume(VoiceHandle voice, float volume);
    [[nodiscard]] float Volume(VoiceHandle voice) const;

    /// A score in layers (ADR-0068): every clip loops on the music bus, started on the same sample. The first
    /// always plays; each next one fades in as the tension rises past its share, so that at full tension all
    /// play. Replaces the layers playing.
    void PlayLayers(std::span<const ClipHandle> layers, float volume = 1.0f);
    void StopLayers();
    /// From 0, calm, to 1; layers follow it over a second or two.
    void SetTension(float tension);
    [[nodiscard]] float Tension() const;
    /// Each layer's volume now, over the layers' volume.
    [[nodiscard]] std::vector<float> LayerLevels() const;
    [[nodiscard]] bool IsPlaying(VoiceHandle voice) const;
    [[nodiscard]] std::size_t VoiceCount() const;

    /// Holds every voice where it is, for a game's pause menu, and lets them go on when unpaused. New
    /// voices do not start while paused.
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const;

    /// Direction and up must be normalized.
    void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

    /// Geometry that stops and muffles sound (ADR-0065), in world space: the level's walls and floors. With
    /// Steam Audio only; otherwise ignored.
    void AddAcousticMesh(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                         const AcousticMaterial& material);
    /// Geometry that moves, as a door: its index for MoveAcousticMesh. positions are in its own space.
    std::size_t AddMovableAcousticMesh(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                                       const AcousticMaterial& material, const glm::mat4& transform);
    void MoveAcousticMesh(std::size_t mesh, const glm::mat4& transform);
    /// Once the static geometry is in: places probes over its floors, spacing meters apart, and bakes the
    /// paths between them on a worker thread (ADR-0066). Sounds then come around corners and through open
    /// doors, from where the way leads. Rooms also get their reverb either way, from reflections traced around
    /// the listener.
    void StartPathing(float spacing = 3.0f);
    [[nodiscard]] PathingState Pathing() const;
    /// Whether sounds are heard through the geometry and an HRTF.
    [[nodiscard]] bool HearsGeometry() const;

    /// Fades the mix to a snapshot over these seconds (ADR-0067).
    void SetSnapshot(const MixSnapshot& snapshot, float fadeSeconds = 0.5f);
    /// The mix as it is now, on its way to the last snapshot, and how much music and ambience are ducked.
    [[nodiscard]] const MixSnapshot& Mix() const;
    [[nodiscard]] float Ducking() const;

    /// Releases the voices that ended, moves the mix towards its snapshot and its ducking, then simulates how
    /// each sound reaches the listener through the geometry. Run it once per frame.
    void Update();
    /// Seconds each Update stands for, instead of the time since the last one: a recording (ADR-0123) reads the
    /// mix faster or slower than it plays. Nothing for the clock.
    void SetFixedStep(std::optional<float> seconds);
    /// Milliseconds the last Update spent simulating, for the sound's CPU budget.
    [[nodiscard]] double LastSimulationMilliseconds() const;
    /// Milliseconds the last round of the worker thread took, every 50 ms: the reverb and the paths.
    [[nodiscard]] double WorkerMilliseconds() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace daidalos::audio
