// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace daidalos;
using audio::AudioEngine;
using audio::ClipHandle;
using audio::PlayOptions;
using audio::SoundWorld;

namespace {

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kBlockFrames = 4800; // A tenth of a second.

void Append(std::vector<char>& bytes, std::uint32_t value, int size) {
    for (int index = 0; index < size; ++index) {
        bytes.push_back(static_cast<char>((value >> (index * 8)) & 0xFF));
    }
}

void AppendText(std::vector<char>& bytes, const std::string& text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

// A 440 Hz tone, in mono.
void WriteTone(const std::filesystem::path& path, float seconds) {
    const auto frames = static_cast<std::uint32_t>(seconds * kSampleRate);
    const std::uint32_t dataSize = frames * 2;
    std::vector<char> bytes;
    AppendText(bytes, "RIFF");
    Append(bytes, 36 + dataSize, 4);
    AppendText(bytes, "WAVEfmt ");
    Append(bytes, 16, 4);
    Append(bytes, 1, 2);               // PCM.
    Append(bytes, 1, 2);               // Channels.
    Append(bytes, kSampleRate, 4);     //
    Append(bytes, kSampleRate * 2, 4); // Bytes per second.
    Append(bytes, 2, 2);               // Bytes per frame.
    Append(bytes, 16, 2);              // Bits per sample.
    AppendText(bytes, "data");
    Append(bytes, dataSize, 4);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * std::numbers::pi * 440.0 * frame / kSampleRate;
        Append(bytes, static_cast<std::uint32_t>(static_cast<std::int16_t>(std::sin(phase) * 20000.0)), 2);
    }
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

struct Levels {
    float left = 0.0f;
    float right = 0.0f;
};

class AudioTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_root = std::filesystem::temp_directory_path() / "daidalos-audio-test";
        std::filesystem::create_directories(m_root);
        WriteTone(m_root / "short.wav", 0.2f);
        WriteTone(m_root / "long.wav", 2.0f);

        audio::AudioConfig config;
        config.useDevice = false;
        config.sampleRate = kSampleRate;
        config.channels = kChannels;
        config.musicVolume = 0.25f;
        core::Result<AudioEngine> engine = AudioEngine::Create(config);
        ASSERT_TRUE(engine.has_value()) << core::Format(engine.error());
        m_engine.emplace(std::move(*engine));
        m_sounds.emplace(&*m_engine, m_root);
    }

    void TearDown() override {
        m_sounds.reset();
        m_engine.reset();
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    ClipHandle Clip(const std::string& path, const audio::ClipOptions& options = {}) {
        const core::Result<ClipHandle> clip = m_sounds->Load(path, options);
        EXPECT_TRUE(clip.has_value()) << core::Format(clip.error());
        return clip.value_or(ClipHandle{});
    }

    // The loudest sample of each ear over the next block.
    Levels Mix() {
        std::vector<float> samples(std::size_t{kBlockFrames} * kChannels);
        m_engine->Read(samples);
        Levels levels;
        for (std::size_t index = 0; index + 1 < samples.size(); index += kChannels) {
            levels.left = std::max(levels.left, std::abs(samples[index]));
            levels.right = std::max(levels.right, std::abs(samples[index + 1]));
        }
        return levels;
    }

    std::filesystem::path m_root;
    std::optional<AudioEngine> m_engine;
    std::optional<SoundWorld> m_sounds;
};

} // namespace

TEST_F(AudioTest, MissingFileIsNotFoundAndNamesIt) {
    const core::Result<ClipHandle> clip = m_sounds->Load("nothing.wav");
    ASSERT_FALSE(clip.has_value());
    EXPECT_EQ(clip.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(clip.error().message.find("nothing.wav"), std::string::npos);
}

TEST_F(AudioTest, InvalidFileIsAnErrorThatNamesIt) {
    std::ofstream(m_root / "broken.wav") << "not a sound";
    const core::Result<ClipHandle> clip = m_sounds->Load("broken.wav");
    ASSERT_FALSE(clip.has_value());
    EXPECT_NE(clip.error().message.find("broken.wav"), std::string::npos);
}

TEST_F(AudioTest, ClipIsLoadedOnce) {
    EXPECT_EQ(Clip("short.wav"), Clip("short.wav"));
    EXPECT_EQ(m_sounds->ClipCount(), 1u);
}

TEST_F(AudioTest, PlayedClipIsHeardThenItsVoiceIsReleased) {
    const audio::VoiceHandle voice = m_sounds->Play(Clip("short.wav"));
    ASSERT_TRUE(voice);
    EXPECT_GT(Mix().left, 0.1f);

    for (int block = 0; block < 4; ++block) {
        Mix();
    }
    m_sounds->Update();
    EXPECT_FALSE(m_sounds->IsPlaying(voice));
    EXPECT_EQ(m_sounds->VoiceCount(), 0u);
    EXPECT_FLOAT_EQ(Mix().left, 0.0f);
}

TEST_F(AudioTest, OneClipCanPlayTwiceAtOnce) {
    const ClipHandle clip = Clip("long.wav");
    const audio::VoiceHandle first = m_sounds->Play(clip);
    const audio::VoiceHandle second = m_sounds->Play(clip);
    EXPECT_NE(first, second);
    EXPECT_EQ(m_sounds->VoiceCount(), 2u);
}

TEST_F(AudioTest, LoopKeepsPlayingUntilStopped) {
    const audio::VoiceHandle voice = m_sounds->Play(Clip("short.wav"), {.loop = true});
    for (int block = 0; block < 10; ++block) {
        Mix();
    }
    m_sounds->Update();
    ASSERT_TRUE(m_sounds->IsPlaying(voice));
    EXPECT_GT(Mix().left, 0.1f);

    m_sounds->Stop(voice);
    EXPECT_FALSE(m_sounds->IsPlaying(voice));
    EXPECT_FLOAT_EQ(Mix().left, 0.0f);
}

TEST_F(AudioTest, SoundOnTheRightIsLouderInTheRightEar) {
    m_sounds->SetListener({0.0f, 0.0f, 0.0f}, math::kForward, math::kUp);
    m_sounds->Play(Clip("long.wav"), {.position = glm::vec3(5.0f, 0.0f, 0.0f)});
    Mix(); // The HRTF turns towards the sound over its first blocks.
    const Levels levels = Mix();
    // Through the HRTF (ADR-0065), a 440 Hz tone at the side is about 1.8 times louder in the near ear.
    EXPECT_GT(levels.right, levels.left * 1.5f);
}

TEST_F(AudioTest, TurningAroundSwapsTheEars) {
    m_sounds->SetListener({0.0f, 0.0f, 0.0f}, -math::kForward, math::kUp);
    m_sounds->Play(Clip("long.wav"), {.position = glm::vec3(5.0f, 0.0f, 0.0f)});
    Mix(); // The HRTF turns towards the sound over its first blocks.
    const Levels levels = Mix();
    EXPECT_GT(levels.left, levels.right * 1.5f);
}

TEST_F(AudioTest, DistantSoundIsQuieter) {
    const ClipHandle clip = Clip("long.wav");
    const audio::VoiceHandle voice = m_sounds->Play(clip, {.position = glm::vec3(0.0f, 0.0f, -2.0f)});
    const float nearby = Mix().left;
    m_sounds->SetPosition(voice, {0.0f, 0.0f, -20.0f});
    Mix(); // Volume changes are smoothed.
    EXPECT_LT(Mix().left, nearby * 0.5f);
}

TEST_F(AudioTest, MusicBusHasItsOwnVolume) {
    const ClipHandle clip = Clip("long.wav");
    const audio::VoiceHandle effect = m_sounds->Play(clip);
    const float full = Mix().left;
    m_sounds->Stop(effect);
    m_sounds->Play(clip, {.bus = audio::Bus::Music});
    EXPECT_NEAR(Mix().left, full * 0.25f, full * 0.05f);
}

TEST_F(AudioTest, VolumesChangeWhilePlaying) {
    m_sounds->Play(Clip("long.wav"), {.loop = true});
    const float full = Mix().left;
    m_engine->SetVolumes(0.5f, 0.5f, 1.0f);
    Mix(); // Volume changes are smoothed.
    EXPECT_NEAR(Mix().left, full * 0.25f, full * 0.05f);
}

TEST_F(AudioTest, PausedVoicesHoldTheirPlaceAndGoOn) {
    const audio::VoiceHandle voice = m_sounds->Play(Clip("short.wav"));
    Mix();
    m_sounds->SetPaused(true);
    EXPECT_TRUE(m_sounds->IsPaused());
    Mix(); // What was already mixed ends.
    for (int block = 0; block < 10; ++block) {
        EXPECT_FLOAT_EQ(Mix().left, 0.0f);
    }
    m_sounds->Update();
    EXPECT_TRUE(m_sounds->IsPlaying(voice));
    EXPECT_FALSE(m_sounds->Play(Clip("long.wav")));

    // The 0.2 s tone had a tenth of a second left.
    m_sounds->SetPaused(false);
    EXPECT_GT(Mix().left, 0.1f);
    Mix();
    m_sounds->Update();
    EXPECT_FALSE(m_sounds->IsPlaying(voice));
}

TEST_F(AudioTest, DestroyingTheWorldSilencesIt) {
    m_sounds->Play(Clip("long.wav"), {.loop = true});
    EXPECT_GT(Mix().left, 0.1f);
    m_sounds.reset();
    EXPECT_FLOAT_EQ(Mix().left, 0.0f);
}

TEST_F(AudioTest, TooManyVoicesAreRefused) {
    const ClipHandle clip = Clip("long.wav");
    audio::VoiceHandle last;
    for (int count = 0; count < 100; ++count) {
        last = m_sounds->Play(clip);
    }
    EXPECT_FALSE(last);
    EXPECT_EQ(m_sounds->VoiceCount(), 64u);
}

TEST_F(AudioTest, NullClipPlaysNothing) {
    EXPECT_FALSE(m_sounds->Play(ClipHandle{}));
}

TEST_F(AudioTest, EmitterMovesItsVoiceToItsEntity) {
    ecs::World world;
    const ecs::Entity entity = world.Create();
    world.Add<ecs::LocalTransform>(entity).translation = glm::vec3(5.0f, 0.0f, 0.0f);
    ecs::PropagateTransforms(world);
    world.Add<scene::SoundEmitter>(entity,
                                   m_sounds->Play(Clip("long.wav"), {.position = glm::vec3(-5.0f, 0.0f, 0.0f)}));

    scene::SyncSoundEmitters(world, *m_sounds);
    Mix(); // Panning is smoothed.
    const Levels levels = Mix();
    EXPECT_GT(levels.right, levels.left * 1.5f);
}

TEST_F(AudioTest, DemoMusicStreamsFromOgg) {
    const std::filesystem::path music = DAI_FACTORY_MUSIC;
    ASSERT_TRUE(std::filesystem::exists(music));
    std::filesystem::copy_file(music, m_root / "music.ogg");
    m_sounds->Play(Clip("music.ogg", {.stream = true}), {.loop = true});
    float loudest = 0.0f;
    // miniaudio decodes streams on its own thread, and the piece starts softly.
    for (int block = 0; block < 400 && loudest <= 0.0f; ++block) {
        loudest = std::max(loudest, Mix().left);
    }
    EXPECT_GT(loudest, 0.0f);
}

TEST_F(AudioTest, StreamedInvalidFileIsAnErrorToo) {
    std::ofstream(m_root / "broken.ogg") << "not a sound";
    EXPECT_FALSE(m_sounds->Load("broken.ogg", {.stream = true}).has_value());
}

TEST(SilentSoundWorld, StillReportsMissingFilesAndPlaysNothing) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "daidalos-silent-audio-test";
    std::filesystem::create_directories(root);
    WriteTone(root / "tone.wav", 0.1f);
    {
        SoundWorld sounds(nullptr, root);
        EXPECT_FALSE(sounds.Load("nothing.wav").has_value());
        const core::Result<ClipHandle> clip = sounds.Load("tone.wav");
        ASSERT_TRUE(clip.has_value());
        const audio::VoiceHandle voice = sounds.Play(*clip);
        EXPECT_FALSE(voice);
        sounds.Stop(voice);
        sounds.SetListener({0.0f, 0.0f, 0.0f}, math::kForward, math::kUp);
        sounds.Update();
        EXPECT_EQ(sounds.VoiceCount(), 0u);
    }
    std::filesystem::remove_all(root);
}

namespace {

// A square of wall across the z axis at z, two triangles, 4 meters wide and high.
void AddWall(std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices, float z) {
    const auto first = static_cast<std::uint32_t>(positions.size());
    positions.insert(positions.end(), {{-2.0f, -2.0f, z}, {2.0f, -2.0f, z}, {2.0f, 2.0f, z}, {-2.0f, 2.0f, z}});
    indices.insert(indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

} // namespace

TEST_F(AudioTest, AWallMuffles) {
    if (!m_sounds->HearsGeometry()) {
        GTEST_SKIP() << "Steam Audio did not start";
    }
    const ClipHandle clip = Clip("long.wav");
    const audio::VoiceHandle voice = m_sounds->Play(clip, {.position = glm::vec3(0.0f, 0.0f, -4.0f)});
    m_sounds->Update();
    Mix();
    const float open = Mix().left;
    m_sounds->Stop(voice);

    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    AddWall(positions, indices, -2.0f);
    m_sounds->AddAcousticMesh(positions, indices, audio::AcousticMaterialOf("concrete"));
    m_sounds->Play(clip, {.position = glm::vec3(0.0f, 0.0f, -4.0f)});
    m_sounds->Update();
    Mix();
    const float walled = Mix().left;
    RecordProperty("open", std::to_string(open));
    RecordProperty("walled", std::to_string(walled));
    EXPECT_LT(walled, open * 0.2f);
}

TEST_F(AudioTest, ASoundBehindADoorIsMuffledUntilItOpens) {
    // The completion criterion of Phase 12 (ADR-0065): a closed metal door lets little through; swung out of
    // the way, the sound comes through.
    if (!m_sounds->HearsGeometry()) {
        GTEST_SKIP() << "Steam Audio did not start";
    }
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    AddWall(positions, indices, 0.0f);
    const std::size_t door = m_sounds->AddMovableAcousticMesh(positions, indices, audio::AcousticMaterialOf("metal"),
                                                              glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -2.0f}));
    m_sounds->Play(Clip("long.wav"), {.position = glm::vec3(0.0f, 0.0f, -4.0f), .loop = true});
    m_sounds->Update();
    Mix();
    const float closed = Mix().left;
    m_sounds->MoveAcousticMesh(door, glm::translate(glm::mat4(1.0f), {10.0f, 0.0f, -2.0f}));
    m_sounds->Update();
    Mix();
    const float opened = Mix().left;
    RecordProperty("closed", std::to_string(closed));
    RecordProperty("opened", std::to_string(opened));
    EXPECT_LT(closed, opened * 0.3f);
    EXPECT_GT(closed, 0.0f);
}

namespace {

// A quad of four corners, two triangles, facing both ways.
void AddQuad(std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices, glm::vec3 a, glm::vec3 b,
             glm::vec3 c, glm::vec3 d) {
    const auto first = static_cast<std::uint32_t>(positions.size());
    positions.insert(positions.end(), {a, b, c, d});
    indices.insert(indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

// Six faces of a box.
void AddRoom(std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices, glm::vec3 low, glm::vec3 high) {
    const auto at = [&](int x, int y, int z) {
        return glm::vec3(x != 0 ? high.x : low.x, y != 0 ? high.y : low.y, z != 0 ? high.z : low.z);
    };
    AddQuad(positions, indices, at(0, 0, 0), at(1, 0, 0), at(1, 0, 1), at(0, 0, 1));
    AddQuad(positions, indices, at(0, 1, 0), at(0, 1, 1), at(1, 1, 1), at(1, 1, 0));
    AddQuad(positions, indices, at(0, 0, 0), at(0, 1, 0), at(1, 1, 0), at(1, 0, 0));
    AddQuad(positions, indices, at(0, 0, 1), at(1, 0, 1), at(1, 1, 1), at(0, 1, 1));
    AddQuad(positions, indices, at(0, 0, 0), at(0, 0, 1), at(0, 1, 1), at(0, 1, 0));
    AddQuad(positions, indices, at(1, 0, 0), at(1, 1, 0), at(1, 1, 1), at(1, 0, 1));
}

} // namespace

TEST_F(AudioTest, ARoomRingsAfterTheSoundStops) {
    // In a closed concrete room, reflections traced around the listener give a reverb (ADR-0066): after the
    // sound stops, its tail goes on.
    if (!m_sounds->HearsGeometry()) {
        GTEST_SKIP() << "Steam Audio did not start";
    }
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    AddRoom(positions, indices, {-6.0f, -2.0f, -8.0f}, {6.0f, 4.0f, 4.0f});
    m_sounds->AddAcousticMesh(positions, indices, audio::AcousticMaterialOf("concrete"));
    const audio::VoiceHandle voice =
        m_sounds->Play(Clip("long.wav"), {.position = glm::vec3(0.0f, 0.0f, -4.0f), .loop = true});
    // The worker traces the room in its own time.
    for (int round = 0; round < 20; ++round) {
        m_sounds->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    for (int block = 0; block < 5; ++block) {
        Mix();
    }
    m_sounds->Stop(voice);
    Mix(); // The voices' last block.
    const float tail = Mix().left;
    RecordProperty("tail", std::to_string(tail));
    EXPECT_GT(tail, 0.001f);
}

TEST_F(AudioTest, SoundFindsTheWayAroundAWall) {
    // A wall between the sound and the listener, with an opening to the side: muffled through the wall, the sound
    // also comes around once the paths are baked (ADR-0066).
    if (!m_sounds->HearsGeometry()) {
        GTEST_SKIP() << "Steam Audio did not start";
    }
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    // A floor for the probes, 1.5 m below the ears; a wall across it from x = -10 to 2, leaving 2 to 10 open.
    AddQuad(positions, indices, {-10.0f, -1.5f, -10.0f}, {10.0f, -1.5f, -10.0f}, {10.0f, -1.5f, 10.0f},
            {-10.0f, -1.5f, 10.0f});
    AddQuad(positions, indices, {-10.0f, -1.5f, -3.0f}, {2.0f, -1.5f, -3.0f}, {2.0f, 3.0f, -3.0f},
            {-10.0f, 3.0f, -3.0f});
    m_sounds->AddAcousticMesh(positions, indices, audio::AcousticMaterialOf("concrete"));
    m_sounds->SetListener({0.0f, 0.0f, 0.0f}, math::kForward, math::kUp);
    const ClipHandle clip = Clip("long.wav");
    const auto level = [&] {
        const audio::VoiceHandle voice = m_sounds->Play(clip, {.position = glm::vec3(0.0f, 0.0f, -6.0f), .loop = true});
        for (int round = 0; round < 8; ++round) {
            m_sounds->Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        Mix();
        const float heard = Mix().left;
        m_sounds->Stop(voice);
        return heard;
    };
    const float muffled = level();
    m_sounds->StartPathing(1.0f);
    for (int wait = 0; wait < 600 && m_sounds->Pathing() == audio::PathingState::Baking; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_EQ(m_sounds->Pathing(), audio::PathingState::Ready);
    const float around = level();
    RecordProperty("muffled", std::to_string(muffled));
    RecordProperty("around", std::to_string(around));
    EXPECT_GT(around, muffled * 2.0f);
}
