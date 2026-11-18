// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Sequence.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace daidalos;
using scene::Cue;

TEST(SequenceTest, EveryKindOfCueParses) {
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(R"({
        "comment": "Each kind once.",
        "cues": [
            {"at": 2.0, "event": "boo"},
            {"at": 0.0, "power": {"circuit": "hall", "on": false}},
            {"at": 0.1, "light": {"entity": "Lamp", "on": false}},
            {"at": 0.2, "sound": {"clip": "a.wav", "entity": "Door", "volume": 0.5, "ducking": 0.3}},
            {"at": 0.3, "particles": {"effect": "sparks.effect.json", "entity": "Cable", "count": 20}},
            {"at": 0.4, "move": {"entity": "Body", "to": [1, 2, 3], "seconds": 1.5}},
            {"at": 0.5, "visible": {"entity": "Ghost", "on": true}},
            {"at": 0.6, "shake": {"strength": 0.8, "seconds": 0.4}},
            {"at": 0.7, "mix": {"music": 0.2, "lowpass": 800, "seconds": 1}},
            {"at": 0.8, "camera": {"keys": [{"at": 1, "position": [0, 2, 0], "target": [0, 1, -5], "focus": 5},
                                            {"at": 0, "position": [0, 2, 3], "target": [0, 1, -5], "aperture": 2}]}}
        ]
    })");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    ASSERT_EQ(sequence->cues.size(), 10u);
    // In time order.
    EXPECT_EQ(sequence->cues.front().kind, Cue::Kind::Power);
    EXPECT_EQ(sequence->cues.back().kind, Cue::Kind::Event);
    EXPECT_EQ(sequence->cues.back().name, "boo");
    const Cue& sound = sequence->cues[2];
    EXPECT_EQ(sound.path, "a.wav");
    EXPECT_EQ(sound.name, "Door");
    EXPECT_FLOAT_EQ(sound.ducking, 0.3f);
    EXPECT_EQ(sequence->cues[3].count, 20u);
    EXPECT_EQ(sequence->cues[4].position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(sequence->cues[7].mix.buses[1], 0.2f);
    EXPECT_FLOAT_EQ(sequence->cues[7].mix.lowpass, 800.0f);
    const Cue& camera = sequence->cues[8];
    ASSERT_EQ(camera.keys.size(), 2u);
    EXPECT_FLOAT_EQ(camera.keys[0].at, 0.0f); // Sorted by time.
    EXPECT_FLOAT_EQ(sequence->Length(), 2.0f);
}

TEST(SequenceTest, MistakesNameWhatIsWrong) {
    const auto error = [](std::string_view json) {
        const core::Result<scene::Sequence> sequence = scene::ParseSequence(json, "test");
        return sequence.has_value() ? std::string() : core::Format(sequence.error());
    };
    EXPECT_NE(error(R"({"cues": [{"at": 0, "dance": {}}]})").find("dance"), std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "shake": {"strenght": 1}}]})").find("strenght"), std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": -1, "event": "x"}]})").find("at"), std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "power": {"on": true}}]})").find("circuit"), std::string::npos);
    EXPECT_NE(error(R"({"steps": []})").find("cues"), std::string::npos);
}

TEST(SequenceTest, TheCameraEasesBetweenItsKeys) {
    const std::vector<scene::CameraKey> keys{{.at = 0.0f, .position = glm::vec3(0.0f), .focus = 2.0f},
                                             {.at = 2.0f, .position = glm::vec3(10.0f, 0.0f, 0.0f), .focus = 6.0f}};
    EXPECT_EQ(scene::CameraAt(keys, -1.0f).position, glm::vec3(0.0f));
    EXPECT_NEAR(scene::CameraAt(keys, 1.0f).position.x, 5.0f, 1e-4f);
    EXPECT_NEAR(scene::CameraAt(keys, 1.0f).focus, 4.0f, 1e-4f);
    // Slow at the ends: a quarter of the way in time is less than a quarter of the way.
    EXPECT_LT(scene::CameraAt(keys, 0.5f).position.x, 2.5f);
    EXPECT_EQ(scene::CameraAt(keys, 3.0f).position, glm::vec3(10.0f, 0.0f, 0.0f));
}

TEST(SequenceTest, WritingAndReadingBackGivesTheSameSequence) {
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(R"({
        "comment": "Each kind once.",
        "cues": [
            {"at": 2.0, "event": "boo"},
            {"at": 0.0, "power": {"circuit": "hall", "on": false}},
            {"at": 0.1, "light": {"entity": "Lamp", "on": false}},
            {"at": 0.2, "sound": {"clip": "a.wav", "entity": "Door", "volume": 0.5, "ducking": 0.3, "caption": "c"}},
            {"at": 0.25, "sound": {"clip": "b.wav"}},
            {"at": 0.3, "particles": {"effect": "sparks.effect.json", "entity": "Cable", "count": 20}},
            {"at": 0.4, "move": {"entity": "Body", "to": [1, 2, 3], "seconds": 1.5}},
            {"at": 0.5, "visible": {"entity": "Ghost", "on": true}},
            {"at": 0.6, "shake": {"strength": 0.8, "seconds": 0.4}},
            {"at": 0.7, "mix": {"music": 0.2, "lowpass": 800, "seconds": 1}},
            {"at": 0.75, "subtitle": {"speaker": "speaker.you", "text": "subtitle.x"}},
            {"at": 0.8, "camera": {"keys": [{"at": 1, "position": [0, 2, 0], "target": [0, 1, -5], "focus": 5},
                                            {"at": 0, "position": [0, 2, 3], "target": [0, 1, -5], "aperture": 2}]}}
        ]
    })");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    EXPECT_EQ(sequence->comment, "Each kind once.");
    const std::string written = scene::WriteSequence(*sequence);
    const core::Result<scene::Sequence> read = scene::ParseSequence(written);
    ASSERT_TRUE(read.has_value()) << core::Format(read.error()) << "\n" << written;
    EXPECT_EQ(*read, *sequence) << written;
    EXPECT_EQ(scene::WriteSequence(*read), written) << "Written twice, the same text";
}

TEST(SequenceTest, EverySequenceOfTheAssetsParses) {
    const std::filesystem::path root = DAI_PROJECT_ASSET_DIR;
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(".sequence.json")) {
            continue;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        const std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const core::Result<scene::Sequence> sequence = scene::ParseSequence(json, name);
        EXPECT_TRUE(sequence.has_value()) << core::Format(sequence.error());
        ++count;
    }
    EXPECT_GE(count, 6U);
}
