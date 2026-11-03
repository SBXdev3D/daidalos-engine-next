// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/audio/Audio.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/core/SlotPool.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace daidalos::audio {

namespace {

constexpr std::string_view kModule = "audio";
constexpr std::size_t kMaxVoices = 64;

core::Error AudioError(core::ErrorCode code, const std::string& message, ma_result result) {
    return core::Error{code, std::string(kModule), std::format("{}: {}", message, ma_result_description(result))};
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

struct SoundWorld::State {
    struct Clip {
        std::filesystem::path file;
        bool stream = false;
        /// Keeps a decoded clip in memory, and is copied by its voices. Never played; null when silent
        /// or when streaming, where each voice opens the file.
        std::unique_ptr<Sound> source;
    };
    struct Voice {
        std::unique_ptr<Sound> sound;
    };

    State(AudioEngine* owner, std::filesystem::path assetRoot) : engine(owner), root(std::move(assetRoot)) {}

    [[nodiscard]] ma_engine* Engine() const { return engine != nullptr ? &engine->Internals().engine : nullptr; }

    AudioEngine* engine;
    std::filesystem::path root;
    // Voices go first: they use the data of their clips.
    core::SlotPool<Clip, ClipTag> clips;
    std::map<std::string, ClipHandle, std::less<>> clipsByPath;
    core::SlotPool<Voice, VoiceTag> voices;
    std::vector<VoiceHandle> playing;
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
        return tl::unexpected(core::Error{core::ErrorCode::NotFound, std::string(kModule),
                                          std::format("Missing sound '{}'", file.generic_string())});
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
    if (engine == nullptr || found == nullptr || state.voices.Size() >= kMaxVoices) {
        return {};
    }

    ma_sound_group* group = state.engine->Internals().Group(options.bus);
    auto voice = std::make_unique<Sound>();
    const ma_uint32 spatial = options.position.has_value() ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result result =
        found->stream ? InitFromFile(engine, found->file, MA_SOUND_FLAG_STREAM | spatial, group, voice->Get())
                      : ma_sound_init_copy(engine, found->source->Get(), spatial, group, voice->Get());
    if (result != MA_SUCCESS) {
        return {};
    }
    voice->MarkInitialized();

    ma_sound* sound = voice->Get();
    ma_sound_set_volume(sound, options.volume);
    ma_sound_set_looping(sound, options.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_doppler_factor(sound, 0.0f);
    if (options.position.has_value()) {
        ma_sound_set_position(sound, options.position->x, options.position->y, options.position->z);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(sound, options.minDistance);
        ma_sound_set_max_distance(sound, options.maxDistance);
    }
    if (ma_sound_start(sound) != MA_SUCCESS) {
        return {};
    }
    const VoiceHandle handle = state.voices.Insert({std::move(voice)});
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
    if (State::Voice* found = m_state->voices.TryGet(voice)) {
        ma_sound_set_position(found->sound->Get(), position.x, position.y, position.z);
    }
}

bool SoundWorld::IsPlaying(VoiceHandle voice) const {
    return m_state->voices.TryGet(voice) != nullptr;
}

std::size_t SoundWorld::VoiceCount() const {
    return m_state->voices.Size();
}

void SoundWorld::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
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
}

} // namespace daidalos::audio
