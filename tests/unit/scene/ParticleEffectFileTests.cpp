// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/ParticleEffectFile.hpp>

#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace daidalos;
using scene::ParseParticleEffect;
using scene::ParticleEffectFile;

namespace {

// The error's message, which must name what is wrong.
std::string ErrorOf(std::string_view json) {
    const core::Result<ParticleEffectFile> parsed = ParseParticleEffect(json);
    EXPECT_FALSE(parsed.has_value());
    if (parsed.has_value()) {
        return {};
    }
    EXPECT_EQ(parsed.error().code, core::ErrorCode::ParseError);
    return parsed.error().message;
}

} // namespace

TEST(ParseParticleEffect, ReadsEveryKey) {
    const core::Result<ParticleEffectFile> parsed = ParseParticleEffect(R"({
        "comment": "Everything an effect file can say.",
        "texture": "fire.png",
        "columns": 8, "rows": 4, "cell": 2, "frames": 16, "frameRate": 24, "loop": false, "randomStartFrame": true,
        "color": [[0, [1, 0.5, 0.25]], [1, [0, 0, 0, 0]]],
        "fadeIn": 0.2,
        "sizeOverLife": [[0, 1], [1, 3]],
        "emissionOverLife": 4,
        "speedOverLife": [[0.5, 0.5]],
        "additive": true, "lit": false, "collides": true, "bounce": 0.6,
        "lifetime": [1, 2], "size": 0.5,
        "orientation": "velocity", "axis": [1, 0, 0], "stretch": 0.05,
        "shape": "cone", "spawnBox": [1, 2, 3], "spawnRadius": 0.25, "direction": [0, 0, -1], "spread": 90,
        "speed": [2, 4], "inheritVelocity": 0.5, "gravity": -0.1, "drag": 0.3, "rotationSpeed": 180, "softness": 0.4,
        "emitter": {"rate": 20, "ratePerMeter": 5, "bursts": [{"time": 0.5, "count": 8, "interval": 2, "repeats": 3}]}
    })");
    ASSERT_TRUE(parsed.has_value()) << core::Format(parsed.error());
    const renderer::ParticleEffectDesc& desc = parsed->desc;
    EXPECT_EQ(parsed->texture, "fire.png");
    EXPECT_EQ(desc.columns, 8U);
    EXPECT_EQ(desc.rows, 4U);
    EXPECT_EQ(desc.cell, 2U);
    EXPECT_EQ(desc.frames, 16U);
    EXPECT_FLOAT_EQ(desc.frameRate, 24.0f);
    EXPECT_FALSE(desc.loop);
    EXPECT_TRUE(desc.randomStartFrame);
    ASSERT_EQ(desc.color.size(), 2U);
    EXPECT_EQ(desc.color[0].color, glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)) << "opacity defaults to 1";
    EXPECT_EQ(desc.color[1].color, glm::vec4(0.0f));
    EXPECT_FLOAT_EQ(desc.fadeIn, 0.2f);
    ASSERT_EQ(desc.sizeOverLife.size(), 2U);
    EXPECT_FLOAT_EQ(desc.sizeOverLife[1].value, 3.0f);
    ASSERT_EQ(desc.emissionOverLife.size(), 1U);
    EXPECT_FLOAT_EQ(desc.emissionOverLife[0].value, 4.0f);
    EXPECT_FLOAT_EQ(desc.speedOverLife[0].time, 0.5f);
    EXPECT_TRUE(desc.additive);
    EXPECT_FALSE(desc.lit);
    EXPECT_TRUE(desc.collides);
    EXPECT_FLOAT_EQ(desc.bounce, 0.6f);
    EXPECT_EQ(desc.lifetime, glm::vec2(1.0f, 2.0f));
    EXPECT_EQ(desc.size, glm::vec2(0.5f)) << "a number is picked between itself and itself";
    EXPECT_EQ(desc.orientation, renderer::ParticleOrientation::Velocity);
    EXPECT_EQ(desc.axis, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(desc.stretch, 0.05f);
    EXPECT_EQ(desc.shape, renderer::ParticleShape::Cone);
    EXPECT_EQ(desc.spawnBox, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(desc.spawnRadius, 0.25f);
    EXPECT_EQ(desc.direction, glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_FLOAT_EQ(desc.spread, glm::radians(90.0f)) << "degrees in files";
    EXPECT_EQ(desc.speed, glm::vec2(2.0f, 4.0f));
    EXPECT_FLOAT_EQ(desc.inheritVelocity, 0.5f);
    EXPECT_FLOAT_EQ(desc.gravity, -0.1f);
    EXPECT_FLOAT_EQ(desc.drag, 0.3f);
    EXPECT_FLOAT_EQ(desc.rotationSpeed, glm::radians(180.0f));
    EXPECT_FLOAT_EQ(desc.softness, 0.4f);
    EXPECT_FLOAT_EQ(parsed->emission.rate, 20.0f);
    EXPECT_FLOAT_EQ(parsed->emission.ratePerMeter, 5.0f);
    ASSERT_EQ(parsed->emission.bursts.size(), 1U);
    EXPECT_FLOAT_EQ(parsed->emission.bursts[0].time, 0.5f);
    EXPECT_EQ(parsed->emission.bursts[0].count, 8U);
    EXPECT_FLOAT_EQ(parsed->emission.bursts[0].interval, 2.0f);
    EXPECT_EQ(parsed->emission.bursts[0].repeats, 3U);
}

TEST(ParseParticleEffect, KeysLeftOutKeepTheirDefaults) {
    const core::Result<ParticleEffectFile> parsed = ParseParticleEffect(R"({"texture": "smoke.png"})");
    ASSERT_TRUE(parsed.has_value()) << core::Format(parsed.error());
    const renderer::ParticleEffectDesc defaults;
    EXPECT_EQ(parsed->desc.columns, defaults.columns);
    EXPECT_EQ(parsed->desc.color.size(), defaults.color.size());
    EXPECT_EQ(parsed->desc.lit, defaults.lit);
    EXPECT_FLOAT_EQ(parsed->emission.rate, scene::ParticleEmission{}.rate);
}

TEST(ParseParticleEffect, UnknownKeysAreErrorsThatNameThem) {
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "colour": [[0, [1, 1, 1]]]})").find("colour"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "emitter": {"rates": 3}})").find("emitter.rates"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "emitter": {"bursts": [{"cout": 3}]}})").find("emitter.bursts.cout"),
              std::string::npos);
}

TEST(ParseParticleEffect, ValuesOfTheWrongKindAreErrorsThatNameTheirKey) {
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "lit": 1})").find("lit"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "columns": -2})").find("columns"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "axis": [0, 1]})").find("axis"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "size": [2, 1]})").find("size"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "orientation": "sideways"})").find("orientation"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "shape": "torus"})").find("shape"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "sizeOverLife": [[2, 1]]})").find("sizeOverLife"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "color": [[0, 1]]})").find("color"), std::string::npos);
}

TEST(ParseParticleEffect, TextureIsRequired) {
    EXPECT_NE(ErrorOf(R"({"columns": 2})").find("texture"), std::string::npos);
}

TEST(ParseParticleEffect, FlipbookMustFitInTheGrid) {
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "columns": 4, "rows": 2, "cell": 6, "frames": 3})").find("frames"),
              std::string::npos);
    EXPECT_TRUE(
        ParseParticleEffect(R"({"texture": "a.png", "columns": 4, "rows": 2, "cell": 6, "frames": 2})").has_value());
}

TEST(ParseParticleEffect, LifetimeMustBePositive) {
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "lifetime": 0})").find("lifetime"), std::string::npos);
}

TEST(ParseParticleEffect, NotJsonOrNotAnObjectIsAnError) {
    EXPECT_FALSE(ErrorOf(R"({"texture": )").empty());
    EXPECT_FALSE(ErrorOf(R"(["texture"])").empty());
}
