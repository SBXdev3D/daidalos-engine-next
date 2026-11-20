// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/EffectDocument.hpp>
#include <daidalos/scene/ParticleEffectFile.hpp>

#include <gtest/gtest.h>

using namespace daidalos;
using scene::EffectDocument;

namespace {

constexpr std::string_view kAsh = R"({
    "comment": "Ash drifting down.",
    "texture": "sprites.png",
    "lifetime": [12, 12],
    "size": [0.01, 0.02],
    "sizeOverLife": 0.5,
    "color": [[0, [0.35, 0.35, 0.35, 0.9]], [1, [0.2, 0.2, 0.2]]],
    "direction": [0, -1, 0],
    "emitter": {"rate": 4}
})";

} // namespace

TEST(EffectDocumentTest, ReadsTheValuesAnEffectHas) {
    const core::Result<EffectDocument> document = EffectDocument::Parse(kAsh);
    ASSERT_TRUE(document.has_value()) << core::Format(document.error());
    EXPECT_EQ(document->Text("texture"), "sprites.png");
    EXPECT_EQ(document->Range("lifetime"), (std::pair{12.0, 12.0}));
    EXPECT_EQ(document->Range("size")->second, 0.02);
    ASSERT_TRUE(document->Curve("sizeOverLife").has_value());
    EXPECT_FLOAT_EQ(document->Curve("sizeOverLife")->front().second, 0.5f);
    ASSERT_EQ(document->Colors("color")->size(), 2U);
    EXPECT_FLOAT_EQ(document->Colors("color")->back().second.a, 1.0f);
    EXPECT_EQ(document->Vector("direction"), glm::vec3(0.0f, -1.0f, 0.0f));
    EXPECT_EQ(document->Number("emitter.rate"), 4.0);
    EXPECT_FALSE(document->Has("gravity"));
}

TEST(EffectDocumentTest, ChangesWriteAFileTheGameReads) {
    core::Result<EffectDocument> document = EffectDocument::Parse(kAsh);
    ASSERT_TRUE(document.has_value());
    document->SetRange("speed", 0.3, 0.6);
    document->SetNumber("gravity", 0.03);
    document->SetNumber("emitter.radius", 3.0);
    document->SetCurve("sizeOverLife", {{0.0f, 1.0f}, {1.0f, 2.0f}});
    document->SetColors("color", {{0.0f, glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)}});
    document->SetRange("lifetime", 5.0, 5.0);
    document->Remove("emitter.rate");
    const std::string text = document->Write();
    // The comment stays; the game's reader takes the file.
    EXPECT_NE(text.find("Ash drifting down."), std::string::npos);
    const core::Result<scene::ParticleEffectFile> effect = scene::ParseParticleEffect(text);
    ASSERT_TRUE(effect.has_value()) << core::Format(effect.error());
    EXPECT_FLOAT_EQ(effect->desc.gravity, 0.03f);
    EXPECT_FLOAT_EQ(effect->desc.speed.y, 0.6f);
    EXPECT_FLOAT_EQ(effect->desc.lifetime.x, 5.0f);
    EXPECT_EQ(effect->desc.sizeOverLife.size(), 2U);
    EXPECT_FLOAT_EQ(effect->emission.radius, 3.0f);
    const core::Result<EffectDocument> again = EffectDocument::Parse(text);
    ASSERT_TRUE(again.has_value());
    EXPECT_FALSE(again->Has("emitter.rate"));
    EXPECT_EQ(again->Number("lifetime"), 5.0);
}

TEST(EffectDocumentTest, NotAnObjectIsAnError) {
    EXPECT_FALSE(EffectDocument::Parse("[1, 2]").has_value());
    EXPECT_FALSE(EffectDocument::Parse("{").has_value());
}
