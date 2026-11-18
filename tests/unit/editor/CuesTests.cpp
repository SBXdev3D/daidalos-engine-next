// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include "Cues.hpp"

using namespace daidalos;

namespace {

TEST(Cues, ANewCueOfEachKindIsWrittenAndReadBack) {
    scene::Sequence sequence;
    float at = 0.0f;
    for (const scene::Cue::Kind kind : editor::kCueKinds) {
        sequence.cues.push_back(editor::MakeCue(kind, at));
        EXPECT_EQ(sequence.cues.back().kind, kind);
        EXPECT_FALSE(editor::CueSummary(sequence.cues.back()).empty());
        at += 0.5f;
    }
    // Each kind is a cue its file can hold, as the panel makes it.
    sequence.cues.back().name = "boo";
    for (scene::Cue& cue : sequence.cues) {
        if (cue.kind == scene::Cue::Kind::Sound) {
            cue.path = "Factory/Sounds/alarm.wav";
        } else if (cue.kind == scene::Cue::Kind::Particles) {
            cue.path = "Factory/Effects/cable_sparks.effect.json";
            cue.name = "Cable";
        } else if (cue.kind == scene::Cue::Kind::Light || cue.kind == scene::Cue::Kind::Visible ||
                   cue.kind == scene::Cue::Kind::Move) {
            cue.name = "Lamp";
        } else if (cue.kind == scene::Cue::Kind::Power) {
            cue.name = "hall";
        } else if (cue.kind == scene::Cue::Kind::Subtitle) {
            cue.caption = "subtitle.x";
        }
    }
    const core::Result<scene::Sequence> read = scene::ParseSequence(scene::WriteSequence(sequence));
    ASSERT_TRUE(read.has_value()) << core::Format(read.error());
    ASSERT_EQ(read->cues.size(), sequence.cues.size());
    EXPECT_GT(read->Length(), 4.0f);
}

TEST(Cues, ACameraCueStartsWithTwoKeys) {
    const scene::Cue camera = editor::MakeCue(scene::Cue::Kind::Camera, 1.0f);
    ASSERT_EQ(camera.keys.size(), 2U);
    EXPECT_LT(camera.keys[0].at, camera.keys[1].at);
    EXPECT_EQ(editor::CueSummary(camera), "camera, 2 keys over 1 s");
}

TEST(Cues, SummariesSayWhatACueDoes) {
    scene::Cue sound = editor::MakeCue(scene::Cue::Kind::Sound, 0.0f);
    sound.path = "Factory/Sounds/zombie_moan.wav";
    sound.name = "Door";
    EXPECT_EQ(editor::CueSummary(sound), "sound zombie_moan.wav at Door");
    EXPECT_NE(editor::CueColor(scene::Cue::Kind::Sound), editor::CueColor(scene::Cue::Kind::Camera));
}

} // namespace
