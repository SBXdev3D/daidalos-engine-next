// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/audio/Audio.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/core/SlotPool.hpp>

#include <glm/geometric.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "SteamAudio.hpp"

namespace daidalos::audio {

namespace {

constexpr std::string_view kModule = "audio";
constexpr std::size_t kMaxVoices = 64;

core::Error AudioError(core::ErrorCode code, const std::string& message, ma_result result) {
    return core::Error{code, kModule, std::format("{}: {}", message, ma_result_description(result))};
}

/// A miniaudio sound, which must not move once it is initialized.
class Sound {
public:
    Sound() = default;
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    ~Sound() {
        if (m_initialized) {
            ma_sound_uninit(&m_sound);
        }
    }

    [[nodiscard]] ma_sound* Get() { return &m_sound; }
    void MarkInitialized() { m_initialized = true; }

private:
    ma_sound m_sound{};
    bool m_initialized = false;
};

ma_result InitFromFile(ma_engine* engine, const std::filesystem::path& path, ma_uint32 flags, ma_sound_group* group,
                       ma_sound* sound) {
#ifdef _WIN32
    return ma_sound_init_from_file_w(engine, path.c_str(), flags, group, nullptr, sound);
#else
    return ma_sound_init_from_file(engine, path.c_str(), flags, group, nullptr, sound);
#endif
}

/// Whether the file can be decoded. miniaudio 0.11.25 reads freed memory when its resource manager
/// fails to decode a file, so nothing unreadable may get that far.
ma_result ProbeFile(const std::filesystem::path& path) {
    ma_decoder decoder;
#ifdef _WIN32
    const ma_result result = ma_decoder_init_file_w(path.c_str(), nullptr, &decoder);
#else
    const ma_result result = ma_decoder_init_file(path.c_str(), nullptr, &decoder);
#endif
    if (result == MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
    }
    return result;
}

} // namespace

struct AudioEngine::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        if (effectsReady) {
            ma_sound_group_uninit(&effects);
        }
        if (musicReady) {
            ma_sound_group_uninit(&music);
        }
        if (engineReady) {
            ma_engine_uninit(&engine);
        }
    }

    [[nodiscard]] ma_sound_group* Group(Bus bus) { return bus == Bus::Music ? &music : &effects; }

    ma_engine engine{};
    ma_sound_group effects{};
    ma_sound_group music{};
    /// Steam Audio's context, or null when sounds are panned by miniaudio (ADR-0065).
    std::unique_ptr<steam::Context> steam;
    bool engineReady = false;
    bool effectsReady = false;
    bool musicReady = false;
    bool hasDevice = false;
};

core::Result<AudioEngine> AudioEngine::Create(const AudioConfig& config) {
    DAI_ASSERT(config.channels > 0 && config.sampleRate > 0, "The output needs a format");

    auto impl = std::make_unique<Impl>();
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.channels = config.channels;
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.noDevice = config.useDevice ? MA_FALSE : MA_TRUE;
    if (const ma_result result = ma_engine_init(&engineConfig, &impl->engine); result != MA_SUCCESS) {
        return tl::unexpected(AudioError(core::ErrorCode::DeviceError, "No audio output", result));
    }
    impl->engineReady = true;
    impl->hasDevice = config.useDevice;

    if (const ma_result result = ma_sound_group_init(&impl->engine, 0, nullptr, &impl->effects); result != MA_SUCCESS) {
        return tl::unexpected(AudioError(core::ErrorCode::DeviceError, "Cannot create the effects bus", result));
    }
    impl->effectsReady = true;
    if (const ma_result result = ma_sound_group_init(&impl->engine, 0, nullptr, &impl->music); result != MA_SUCCESS) {
        return tl::unexpected(AudioError(core::ErrorCode::DeviceError, "Cannot create the music bus", result));
    }
    impl->musicReady = true;
    if (config.steamAudio) {
        impl->steam = steam::Context::Create(config.sampleRate);
    }

    ma_engine_set_volume(&impl->engine, config.volume);
    ma_sound_group_set_volume(&impl->effects, config.effectsVolume);
    ma_sound_group_set_volume(&impl->music, config.musicVolume);
    return AudioEngine(std::move(impl));
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
AudioEngine::AudioEngine(AudioEngine&& other) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&& other) noexcept = default;
AudioEngine::~AudioEngine() = default;

void AudioEngine::Read(std::span<float> samples) {
    DAI_ASSERT(!m_impl->hasDevice, "The device reads the mixer by itself");
    const ma_uint32 channels = ma_engine_get_channels(&m_impl->engine);
    ma_engine_read_pcm_frames(&m_impl->engine, samples.data(), samples.size() / channels, nullptr);
}

void AudioEngine::SetVolumes(float volume, float effectsVolume, float musicVolume) {
    ma_engine_set_volume(&m_impl->engine, std::clamp(volume, 0.0f, 1.0f));
    ma_sound_group_set_volume(&m_impl->effects, std::clamp(effectsVolume, 0.0f, 1.0f));
    ma_sound_group_set_volume(&m_impl->music, std::clamp(musicVolume, 0.0f, 1.0f));
}

struct SoundWorld::State {
    struct Clip {
        std::filesystem::path file;
        bool stream = false;
        /// Keeps a decoded clip in memory, and is copied by its voices. Never played; null when silent
        /// or when streaming, where each voice opens the file.
        std::unique_ptr<Sound> source;
    };
    /// A voice's source in the acoustic simulation, removed with it.
    struct Source {
        steam::Scene* scene = nullptr;
        IPLSource source = nullptr;
        Source() = default;
        Source(steam::Scene* owner, IPLSource added) : scene(owner), source(added) {}
        Source(Source&& other) noexcept
            : scene(std::exchange(other.scene, nullptr)), source(std::exchange(other.source, nullptr)) {}
        Source& operator=(Source&& other) noexcept {
            std::swap(scene, other.scene);
            std::swap(source, other.source);
            return *this;
        }
        Source(const Source&) = delete;
        Source& operator=(const Source&) = delete;
        ~Source() {
            if (scene != nullptr) {
                scene->RemoveSource(source);
            }
        }
    };
    struct Voice {
        // Destroyed last to first: the sound stops before the node it plays into goes.
        Source source;
        std::unique_ptr<steam::SpatialNode> node;
        std::unique_ptr<Sound> sound;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 35.0f;
    };

    State(AudioEngine* owner, std::filesystem::path assetRoot) : engine(owner), root(std::move(assetRoot)) {}

    [[nodiscard]] ma_engine* Engine() const { return engine != nullptr ? &engine->Internals().engine : nullptr; }

    [[nodiscard]] steam::Context* Steam() const {
        return engine != nullptr ? engine->Internals().steam.get() : nullptr;
    }
    /// The acoustic scene, made when the first geometry or spatial voice needs it.
    steam::Scene* AcousticScene() {
        if (scene == nullptr && Steam() != nullptr) {
            scene = steam::Scene::Create(*Steam());
        }
        return scene.get();
    }
    void Simulate(std::span<const VoiceHandle> handles) {
        if (scene == nullptr) {
            return;
        }
        std::vector<steam::Scene::Voice> simulated;
        for (const VoiceHandle handle : handles) {
            Voice& voice = voices.Get(handle);
            if (voice.node != nullptr && voice.source.source != nullptr) {
                simulated.push_back(
                    {voice.source.source, voice.position, voice.minDistance, voice.maxDistance, voice.node.get()});
            }
        }
        scene->Simulate(listener, simulated);
    }

    AudioEngine* engine;
    std::filesystem::path root;
    steam::Scene::Listener listener;
    // The acoustic scene outlives the voices, whose sources it holds.
    std::unique_ptr<steam::Scene> scene;
    double simulationMilliseconds = 0.0;
    // Voices go first: they use the data of their clips.
    core::SlotPool<Clip, ClipTag> clips;
    std::map<std::string, ClipHandle, std::less<>> clipsByPath;
    core::SlotPool<Voice, VoiceTag> voices;
    std::vector<VoiceHandle> playing;
    bool paused = false;
};

SoundWorld::SoundWorld(AudioEngine* engine, std::filesystem::path root)
    : m_state(std::make_unique<State>(engine, std::move(root))) {}
SoundWorld::SoundWorld(SoundWorld&&) noexcept = default;
SoundWorld& SoundWorld::operator=(SoundWorld&&) noexcept = default;
SoundWorld::~SoundWorld() = default;

core::Result<ClipHandle> SoundWorld::Load(std::string_view path, const ClipOptions& options) {
    DAI_PROFILE_FUNCTION();
    State& state = *m_state;
    if (const auto found = state.clipsByPath.find(path); found != state.clipsByPath.end()) {
        return found->second;
    }

    const std::filesystem::path file = state.root / path;
    if (std::error_code error; !std::filesystem::is_regular_file(file, error)) {
        return tl::unexpected(
            core::Error{core::ErrorCode::NotFound, kModule, std::format("Missing sound '{}'", file.generic_string())});
    }

    if (const ma_result result = ProbeFile(file); result != MA_SUCCESS) {
        return tl::unexpected(
            AudioError(core::ErrorCode::ParseError, std::format("Cannot read '{}'", file.generic_string()), result));
    }

    State::Clip clip{file, options.stream, nullptr};
    if (ma_engine* engine = state.Engine(); engine != nullptr && !options.stream) {
        auto source = std::make_unique<Sound>();
        const ma_result result = InitFromFile(engine, file, MA_SOUND_FLAG_DECODE, nullptr, source->Get());
        if (result != MA_SUCCESS) {
            return tl::unexpected(AudioError(core::ErrorCode::ParseError,
                                             std::format("Cannot read '{}'", file.generic_string()), result));
        }
        source->MarkInitialized();
        clip.source = std::move(source);
    }
    const ClipHandle handle = state.clips.Insert(std::move(clip));
    state.clipsByPath.emplace(std::string(path), handle);
    return handle;
}

std::size_t SoundWorld::ClipCount() const {
    return m_state->clips.Size();
}

VoiceHandle SoundWorld::Play(ClipHandle clip, const PlayOptions& options) {
    State& state = *m_state;
    ma_engine* engine = state.Engine();
    State::Clip* found = state.clips.TryGet(clip);
    if (engine == nullptr || found == nullptr || state.paused || state.voices.Size() >= kMaxVoices) {
        return {};
    }

    ma_sound_group* group = state.engine->Internals().Group(options.bus);
    // With Steam Audio, a sound in the world plays into its own node, which hears it through the geometry
    // and the HRTF (ADR-0065); miniaudio pans it otherwise.
    State::Voice playing;
    if (options.position.has_value() && state.AcousticScene() != nullptr) {
        playing.node = steam::SpatialNode::Create(*state.Steam(), engine, reinterpret_cast<ma_node*>(group));
    }
    auto voice = std::make_unique<Sound>();
    const bool steamed = playing.node != nullptr;
    const ma_uint32 spatial = options.position.has_value() && !steamed ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_sound_group* into = steamed ? nullptr : group;
    const ma_result result = found->stream
                                 ? InitFromFile(engine, found->file, MA_SOUND_FLAG_STREAM | spatial, into, voice->Get())
                                 : ma_sound_init_copy(engine, found->source->Get(), spatial, into, voice->Get());
    if (result != MA_SUCCESS) {
        return {};
    }
    voice->MarkInitialized();
    if (steamed) {
        ma_node_attach_output_bus(voice->Get(), 0, playing.node->Node(), 0);
        playing.source = State::Source(state.scene.get(), state.scene->AddSource());
        playing.position = *options.position;
        playing.minDistance = std::max(options.minDistance, 0.01f);
        playing.maxDistance = options.maxDistance;
    }

    ma_sound* sound = voice->Get();
    ma_sound_set_volume(sound, options.volume);
    ma_sound_set_looping(sound, options.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_doppler_factor(sound, 0.0f);
    if (options.position.has_value() && !steamed) {
        ma_sound_set_position(sound, options.position->x, options.position->y, options.position->z);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(sound, options.minDistance);
        ma_sound_set_max_distance(sound, options.maxDistance);
    }
    playing.sound = std::move(voice);
    const VoiceHandle handle = state.voices.Insert(std::move(playing));
    if (steamed) {
        // Heard through the walls from its first sample.
        const std::array<VoiceHandle, 1> just{handle};
        state.Simulate(just);
    }
    if (ma_sound_start(state.voices.Get(handle).sound->Get()) != MA_SUCCESS) {
        state.voices.Remove(handle);
        return {};
    }
    state.playing.push_back(handle);
    return handle;
}

void SoundWorld::Stop(VoiceHandle voice) {
    State& state = *m_state;
    if (state.voices.Remove(voice).has_value()) {
        std::erase(state.playing, voice);
    }
}

void SoundWorld::SetPosition(VoiceHandle voice, const glm::vec3& position) {
    State& state = *m_state;
    if (State::Voice* found = state.voices.TryGet(voice)) {
        found->position = position;
        if (found->node == nullptr) {
            ma_sound_set_position(found->sound->Get(), position.x, position.y, position.z);
        } else if (state.scene != nullptr) {
            state.scene->Place(state.listener, {found->source.source, position, found->minDistance, found->maxDistance,
                                                found->node.get()});
        }
    }
}

bool SoundWorld::IsPlaying(VoiceHandle voice) const {
    return m_state->voices.TryGet(voice) != nullptr;
}

std::size_t SoundWorld::VoiceCount() const {
    return m_state->voices.Size();
}

void SoundWorld::SetPaused(bool paused) {
    State& state = *m_state;
    if (state.paused == paused) {
        return;
    }
    state.paused = paused;
    // Stopping keeps a sound's place: starting it again goes on from there.
    for (const VoiceHandle handle : state.playing) {
        ma_sound* sound = state.voices.Get(handle).sound->Get();
        if (paused) {
            ma_sound_stop(sound);
        } else {
            ma_sound_start(sound);
        }
    }
}

bool SoundWorld::IsPaused() const {
    return m_state->paused;
}

void SoundWorld::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    m_state->listener = {position, forward, up};
    if (ma_engine* engine = m_state->Engine()) {
        ma_engine_listener_set_position(engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(engine, 0, up.x, up.y, up.z);
    }
}

void SoundWorld::Update() {
    State& state = *m_state;
    std::erase_if(state.playing, [&state](VoiceHandle handle) {
        State::Voice& voice = state.voices.Get(handle);
        if (ma_sound_at_end(voice.sound->Get()) == MA_FALSE) {
            return false;
        }
        state.voices.Remove(handle);
        return true;
    });
    const auto start = std::chrono::steady_clock::now();
    state.Simulate(state.playing);
    state.simulationMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double SoundWorld::LastSimulationMilliseconds() const {
    return m_state->simulationMilliseconds;
}

bool SoundWorld::HearsGeometry() const {
    return m_state->Steam() != nullptr;
}

void SoundWorld::AddAcousticMesh(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                                 const AcousticMaterial& material) {
    if (steam::Scene* scene = m_state->AcousticScene()) {
        scene->AddStatic(positions, indices, material);
    }
}

std::size_t SoundWorld::AddMovableAcousticMesh(std::span<const glm::vec3> positions,
                                               std::span<const std::uint32_t> indices, const AcousticMaterial& material,
                                               const glm::mat4& transform) {
    if (steam::Scene* scene = m_state->AcousticScene()) {
        return scene->AddMovable(positions, indices, material, transform);
    }
    return 0;
}

void SoundWorld::MoveAcousticMesh(std::size_t mesh, const glm::mat4& transform) {
    if (m_state->scene != nullptr) {
        m_state->scene->Move(mesh, transform);
    }
}

AcousticMaterial AcousticMaterialOf(std::string_view surface) {
    // Steam Audio's presets (phonon's material table), by band: low, middle, high.
    if (surface == "concrete") {
        return {{0.05f, 0.07f, 0.08f}, 0.05f, {0.015f, 0.002f, 0.001f}};
    }
    if (surface == "metal") {
        return {{0.20f, 0.07f, 0.06f}, 0.05f, {0.200f, 0.025f, 0.010f}};
    }
    if (surface == "wood") {
        return {{0.11f, 0.07f, 0.06f}, 0.05f, {0.070f, 0.014f, 0.005f}};
    }
    if (surface == "glass") {
        return {{0.06f, 0.03f, 0.02f}, 0.05f, {0.060f, 0.044f, 0.011f}};
    }
    if (surface == "tile" || surface == "ceramic") {
        return {{0.01f, 0.02f, 0.02f}, 0.05f, {0.060f, 0.044f, 0.011f}};
    }
    if (surface == "cloth" || surface == "carpet" || surface == "flesh") {
        return {{0.24f, 0.69f, 0.73f}, 0.05f, {0.020f, 0.005f, 0.003f}};
    }
    if (surface == "stone" || surface == "rock") {
        return {{0.13f, 0.20f, 0.24f}, 0.05f, {0.015f, 0.002f, 0.001f}};
    }
    if (surface == "plaster") {
        return {{0.12f, 0.06f, 0.04f}, 0.05f, {0.056f, 0.056f, 0.004f}};
    }
    if (surface == "brick") {
        return {{0.03f, 0.04f, 0.07f}, 0.05f, {0.015f, 0.015f, 0.015f}};
    }
    if (surface == "gravel") {
        return {{0.60f, 0.70f, 0.80f}, 0.05f, {0.031f, 0.012f, 0.008f}};
    }
    return {};
}

} // namespace daidalos::audio
