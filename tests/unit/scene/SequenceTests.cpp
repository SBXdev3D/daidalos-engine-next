// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Sequence.hpp>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

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

TEST(SequenceTest, PhysicsCuesParseAndRoundTrip) {
    // ADR-0127.
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(R"({"cues": [
        {"at": 0.0, "blast": {"entity": "Blast A", "radius": 6, "strength": 400}},
        {"at": 0.1, "impulse": {"entity": "Crate", "impulse": [0, 50, -20], "point": [0, 0.3, 0]}},
        {"at": 0.2, "release": {"entity": "Hanging crate"}},
        {"at": 0.3, "break": {"entity": "Sign", "joint": 1}},
        {"at": 0.4, "motor": {"entity": "Wheel", "joint": 0, "mode": "position", "target": 1.5, "strength": 900}},
        {"at": 0.5, "shot": {"entity": "Lamp", "point": [0, -0.5, 0], "from": [4, 1.5, 2], "weapon": "rifle"}},
        {"at": 0.6, "shot": {"entity": "Door", "from": [1, 1, 1]}}
    ]})");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    ASSERT_EQ(sequence->cues.size(), 7u);
    EXPECT_EQ(sequence->cues[0].kind, Cue::Kind::Blast);
    EXPECT_FLOAT_EQ(sequence->cues[0].radius, 6.0f);
    EXPECT_FLOAT_EQ(sequence->cues[0].strength, 400.0f);
    EXPECT_EQ(sequence->cues[1].vector, glm::vec3(0.0f, 50.0f, -20.0f));
    EXPECT_EQ(sequence->cues[1].point, glm::vec3(0.0f, 0.3f, 0.0f));
    EXPECT_EQ(sequence->cues[2].kind, Cue::Kind::Release);
    EXPECT_EQ(sequence->cues[3].joint, 1u);
    EXPECT_EQ(sequence->cues[4].motor.mode, physics::MotorMode::Position);
    EXPECT_FLOAT_EQ(sequence->cues[4].motor.strength, 900.0f);
    // ADR-0129.
    EXPECT_EQ(sequence->cues[5].kind, Cue::Kind::Shot);
    EXPECT_EQ(sequence->cues[5].point, glm::vec3(0.0f, -0.5f, 0.0f));
    EXPECT_EQ(sequence->cues[5].position, glm::vec3(4.0f, 1.5f, 2.0f));
    EXPECT_EQ(sequence->cues[5].path, "rifle");
    EXPECT_EQ(sequence->cues[6].path, "handgun"); // The weapon unless one is named.
    const core::Result<scene::Sequence> read = scene::ParseSequence(scene::WriteSequence(*sequence));
    ASSERT_TRUE(read.has_value()) << core::Format(read.error());
    EXPECT_EQ(*read, *sequence);

    const auto error = [](std::string_view json) {
        const core::Result<scene::Sequence> bad = scene::ParseSequence(json, "test");
        return bad.has_value() ? std::string() : core::Format(bad.error());
    };
    EXPECT_NE(error(R"({"cues": [{"at": 0, "blast": {"entity": "A", "radius": -1}}]})").find("radius"),
              std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "impulse": {"entity": "A"}}]})").find("impulse"), std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "break": {"entity": "A", "joint": 0.5}}]})").find("joint"),
              std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "motor": {"entity": "A", "mode": "fast"}}]})").find("mode"),
              std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "shot": {"entity": "A"}}]})").find("from"), std::string::npos);
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
    EXPECT_NE(error(R"({"cues": [{"at": 0, "fade": {"to": 2}}]})").find("to"), std::string::npos);
    EXPECT_NE(error(R"({"cues": [{"at": 0, "fade": {"to": 1, "seconds": -1}}]})").find("seconds"), std::string::npos);
    EXPECT_NE(
        error(R"({"cues": [{"at": 0, "camera": {"path": "wobbly", "keys": [{"position": [0, 0, 0]}]}}]})").find("path"),
        std::string::npos);
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

TEST(SequenceTest, ASmoothPathGoesThroughEveryKeyWithoutStopping) {
    // Three keys on a line, unevenly timed (ADR-0120): the camera is at each key at its time, and moves on
    // through the middle one, where the eased path stops.
    const std::vector<scene::CameraKey> keys{
        {.at = 0.0f, .position = {0.0f, 0.0f, 0.0f}, .target = {0.0f, 0.0f, -1.0f}},
        {.at = 2.0f, .position = {0.0f, 0.0f, -10.0f}, .target = {0.0f, 0.0f, -11.0f}},
        {.at = 3.0f, .position = {0.0f, 0.0f, -20.0f}, .target = {0.0f, 0.0f, -21.0f}}};
    const auto at = [&](float time, scene::CameraPath path) { return scene::CameraAt(keys, time, path).position; };
    using scene::CameraPath;
    for (const scene::CameraKey& key : keys) {
        EXPECT_NEAR(glm::distance(at(key.at, CameraPath::Smooth), key.position), 0.0f, 1e-4f);
    }
    const float smoothSpeed = glm::distance(at(2.01f, CameraPath::Smooth), at(1.99f, CameraPath::Smooth)) / 0.02f;
    const float easedSpeed = glm::distance(at(2.01f, CameraPath::Eased), at(1.99f, CameraPath::Eased)) / 0.02f;
    EXPECT_GT(smoothSpeed, 5.0f);
    EXPECT_LT(easedSpeed, 0.5f);
    // The speed changes smoothly through the key: the same just before and just after.
    const float before = glm::distance(at(1.99f, CameraPath::Smooth), at(1.98f, CameraPath::Smooth));
    const float after = glm::distance(at(2.02f, CameraPath::Smooth), at(2.01f, CameraPath::Smooth));
    EXPECT_NEAR(before, after, 0.01f);
    // At rest at both ends, and past them it stays at the ends.
    EXPECT_LT(glm::distance(at(0.01f, CameraPath::Smooth), keys[0].position), 0.01f);
    EXPECT_NEAR(glm::distance(at(9.0f, CameraPath::Smooth), keys[2].position), 0.0f, 1e-4f);
}

TEST(SequenceTest, ASmoothPathTurnsTheShorterWayAndNeverLooksStraightDown) {
    // Yaw is 0 towards -Z and grows to the left. The shorter way from 170 to -170 degrees is through 180,
    // towards +Z, not the long way round through 0.
    const auto direction = [](float yawDegrees) {
        const float yaw = glm::radians(yawDegrees);
        return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
    };
    const std::vector<scene::CameraKey> turn{{.at = 0.0f, .position = glm::vec3(0.0f), .target = direction(170.0f)},
                                             {.at = 1.0f, .position = glm::vec3(0.0f), .target = direction(-170.0f)}};
    const scene::CameraKey half = scene::CameraAt(turn, 0.5f, scene::CameraPath::Smooth);
    const glm::vec3 looking = glm::normalize(half.target - half.position);
    EXPECT_GT(looking.z, 0.99f) << "Looks behind (+Z), half way through a 20 degree turn";

    // A key looking straight down: the camera stops a degree short, so that it still knows which way is up.
    const std::vector<scene::CameraKey> down{
        {.at = 0.0f, .position = {0.0f, 5.0f, 0.0f}, .target = {0.0f, 5.0f, -1.0f}},
        {.at = 1.0f, .position = {0.0f, 5.0f, 0.0f}, .target = {0.0f, 0.0f, 0.0f}}};
    const scene::CameraKey end = scene::CameraAt(down, 1.0f, scene::CameraPath::Smooth);
    const glm::vec3 view = glm::normalize(end.target - end.position);
    EXPECT_LT(view.y, -0.999f);
    EXPECT_GT(view.y, -0.99999f);
}

TEST(SequenceTest, ASmoothPathHoldsALookBetweenTwoKeysThatShareIt) {
    // Level, then up a shaft looking straight up at two keys, then level again (ADR-0120): between the two
    // looking up, the look stays where they are rather than swinging past them and spinning the picture.
    const auto key = [](float at, float y, float pitchDegrees) {
        const float pitch = glm::radians(pitchDegrees);
        const glm::vec3 position{0.0f, y, 0.0f};
        return scene::CameraKey{
            .at = at, .position = position, .target = position + glm::vec3(0.0f, std::sin(pitch), -std::cos(pitch))};
    };
    const std::vector<scene::CameraKey> keys{key(0.0f, 0.0f, 0.0f), key(2.0f, 0.0f, 80.0f), key(4.0f, 5.0f, 80.0f),
                                             key(6.0f, 10.0f, 0.0f)};
    for (float time = 2.0f; time <= 4.0f; time += 0.1f) {
        const scene::CameraKey at = scene::CameraAt(keys, time, scene::CameraPath::Smooth);
        const float pitch = glm::degrees(std::asin(glm::normalize(at.target - at.position).y));
        EXPECT_NEAR(pitch, 80.0f, 0.01f) << "at " << time << " s";
    }
}

TEST(SequenceTest, TheFactoryTourNeverSnaps) {
    // The tour (ADR-0120) at 60 frames a second: from one frame to the next, neither where the camera looks
    // nor which way is up in its picture (as the scene builds its view) turns more than a few degrees, down
    // and up the shafts included.
    std::ifstream file(std::filesystem::path(DAI_PROJECT_ASSET_DIR) / "Factory/Sequences/tour.sequence.json",
                       std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(json, "tour");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    const auto camera = std::ranges::find(sequence->cues, Cue::Kind::Camera, &Cue::kind);
    ASSERT_NE(camera, sequence->cues.end());
    const auto frame = [&](float time) {
        const scene::CameraKey key = scene::CameraAt(camera->keys, time, camera->cameraPath);
        const glm::vec3 forward = glm::normalize(key.target - key.position);
        const glm::vec3 up =
            std::abs(forward.y) > 0.99999f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        return std::pair{forward, glm::normalize(glm::cross(forward, up))};
    };
    const auto degrees = [](const glm::vec3& a, const glm::vec3& b) {
        return glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f)));
    };
    float worstLook = 0.0f;
    float worstRight = 0.0f;
    float worstAt = 0.0f;
    auto previous = frame(0.0f);
    for (float time = 1.0f / 60.0f; time <= camera->keys.back().at; time += 1.0f / 60.0f) {
        const auto now = frame(time);
        worstLook = std::max(worstLook, degrees(previous.first, now.first));
        if (const float right = degrees(previous.second, now.second); right > worstRight) {
            worstRight = right;
            worstAt = time;
        }
        previous = now;
    }
    EXPECT_LT(worstLook, 3.0f);
    EXPECT_LT(worstRight, 3.0f) << "at " << worstAt << " s";
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
                                            {"at": 0, "position": [0, 2, 3], "target": [0, 1, -5], "aperture": 2}]}},
            {"at": 0.9, "camera": {"path": "smooth", "keys": [{"at": 0, "position": [0, 2, 0], "target": [0, 2, -1]}]}},
            {"at": 0.95, "fade": {"to": 1, "seconds": 0.5, "color": [1, 0.5, 0]}},
            {"at": 1.5, "fade": {"to": 0}}
        ]
    })");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    EXPECT_EQ(sequence->comment, "Each kind once.");
    EXPECT_EQ(std::ranges::count(sequence->cues, scene::CameraPath::Smooth, &Cue::cameraPath), 1);
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
