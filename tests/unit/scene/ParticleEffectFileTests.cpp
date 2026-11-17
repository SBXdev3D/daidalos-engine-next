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
        "additive": true, "lit": false, "collision": "stick", "bounce": 0.6, "reportHits": true,
        "onDeath": {"effect": "smoke.effect.json", "count": 2}, "onHit": {"effect": "../splash.effect.json", "count": 5},
        "turbulence": 1.5, "turbulenceScale": 0.25, "anisotropy": 0.5, "distortion": 0.01,
        "traced": false, "fog_density": 1.5,
        "light": {"color": [1, 0.5, 0.2], "temperature": 1800, "intensity": 40, "range": 6, "radius": 0.3,
                  "offset": [0, 0.4, 0], "fade": 0.2, "flickerRate": 9, "flickerDepth": 0.3},
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
    EXPECT_EQ(desc.collision, renderer::ParticleCollision::Stick);
    EXPECT_TRUE(desc.reportHits);
    EXPECT_EQ(parsed->deathEffect, "smoke.effect.json");
    EXPECT_EQ(desc.onDeath.count, 2U);
    EXPECT_EQ(parsed->hitEffect, "../splash.effect.json");
    EXPECT_EQ(desc.onHit.count, 5U);
    EXPECT_FLOAT_EQ(desc.turbulence, 1.5f);
    EXPECT_FLOAT_EQ(desc.turbulenceScale, 0.25f);
    EXPECT_FLOAT_EQ(desc.anisotropy, 0.5f);
    EXPECT_FLOAT_EQ(desc.distortion, 0.01f);
    EXPECT_FALSE(desc.traced);
    EXPECT_FLOAT_EQ(desc.fogDensity, 1.5f);
    ASSERT_TRUE(parsed->emission.light.has_value());
    const scene::EmitterLight& light = *parsed->emission.light;
    EXPECT_EQ(light.color, glm::vec3(1.0f, 0.5f, 0.2f));
    EXPECT_FLOAT_EQ(light.temperature, 1800.0f);
    EXPECT_FLOAT_EQ(light.intensity, 40.0f);
    EXPECT_FLOAT_EQ(light.range, 6.0f);
    EXPECT_FLOAT_EQ(light.radius, 0.3f);
    EXPECT_EQ(light.offset, glm::vec3(0.0f, 0.4f, 0.0f));
    EXPECT_FLOAT_EQ(light.fade, 0.2f);
    EXPECT_FLOAT_EQ(light.flickerRate, 9.0f);
    EXPECT_FLOAT_EQ(light.flickerDepth, 0.3f);
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
    EXPECT_TRUE(parsed->desc.traced);
    EXPECT_EQ(parsed->desc.fogDensity, 0.0f);
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
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "collision": "explode"})").find("collision"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "onHit": {"count": 2}})").find("onHit.effect"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "onDeath": {"effect": "a.json", "cuont": 2}})").find("onDeath.cuont"),
              std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "turbulenceScale": 0})").find("turbulenceScale"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "light": {"intensty": 3}})").find("light.intensty"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "lightMaps": ["a.png"]})").find("lightMaps"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "sizeOverLife": [[2, 1]]})").find("sizeOverLife"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"texture": "a.png", "color": [[0, 1]]})").find("color"), std::string::npos);
}

TEST(ParseParticleEffect, TextureIsRequired) {
    EXPECT_NE(ErrorOf(R"({"columns": 2})").find("texture"), std::string::npos);
}

TEST(ParseParticleEffect, EmitterSaysWhereItMayBeSeen) {
    const core::Result<ParticleEffectFile> parsed =
        ParseParticleEffect(R"({"texture": "a.png", "emitter": {"distance": 30, "radius": 4}})");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_FLOAT_EQ(parsed->emission.distance, 30.0f);
    EXPECT_FLOAT_EQ(parsed->emission.radius, 4.0f);
}

TEST(ParseParticleEffect, AMeshEffectNeedsNoTexture) {
    const core::Result<ParticleEffectFile> parsed = ParseParticleEffect(R"({"mesh": "Meshes/casing.glb"})");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->mesh, "Meshes/casing.glb");
    EXPECT_TRUE(parsed->texture.empty());
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

namespace {

std::string RibbonErrorOf(std::string_view json) {
    const core::Result<scene::RibbonEffectFile> parsed = scene::ParseRibbonEffect(json);
    EXPECT_FALSE(parsed.has_value());
    if (parsed.has_value()) {
        return {};
    }
    EXPECT_EQ(parsed.error().code, core::ErrorCode::ParseError);
    return parsed.error().message;
}

} // namespace

TEST(ParseRibbonEffect, ReadsEveryKey) {
    const core::Result<scene::RibbonEffectFile> parsed = scene::ParseRibbonEffect(R"({
        "comment": "An arc.",
        "texture": "glow.png",
        "additive": false,
        "softness": 0.2,
        "shape": "bolt",
        "color": [[0, [1, 2, 3, 0.5]], [1, [4, 5, 6]]],
        "width": [[0, 0.1], [1, 0.3]],
        "length": 2,
        "tiling": 0.5,
        "scroll": 3,
        "generations": 6,
        "displacement": 0.2,
        "branches": 0.4,
        "refresh": 0.1,
        "flicker": 0.5,
        "duration": 2,
        "spacing": 0.1,
        "light": {"intensity": 30, "range": 4}
    })");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    const scene::RibbonEffect& effect = parsed->effect;
    EXPECT_EQ(parsed->texture, "glow.png");
    EXPECT_FALSE(effect.additive);
    EXPECT_FLOAT_EQ(effect.softness, 0.2f);
    EXPECT_EQ(effect.shape, scene::RibbonShape::Bolt);
    ASSERT_EQ(effect.color.size(), 2U);
    EXPECT_EQ(effect.color[0].color, glm::vec4(1.0f, 2.0f, 3.0f, 0.5f));
    EXPECT_EQ(effect.color[1].color, glm::vec4(4.0f, 5.0f, 6.0f, 1.0f));
    ASSERT_EQ(effect.width.size(), 2U);
    EXPECT_FLOAT_EQ(effect.width[1].value, 0.3f);
    EXPECT_FLOAT_EQ(effect.length, 2.0f);
    EXPECT_FLOAT_EQ(effect.tiling, 0.5f);
    EXPECT_FLOAT_EQ(effect.scroll, 3.0f);
    EXPECT_EQ(effect.generations, 6U);
    EXPECT_FLOAT_EQ(effect.displacement, 0.2f);
    EXPECT_FLOAT_EQ(effect.branches, 0.4f);
    EXPECT_FLOAT_EQ(effect.refresh, 0.1f);
    EXPECT_FLOAT_EQ(effect.flicker, 0.5f);
    EXPECT_FLOAT_EQ(effect.duration, 2.0f);
    EXPECT_FLOAT_EQ(effect.spacing, 0.1f);
    ASSERT_TRUE(effect.light.has_value());
    EXPECT_FLOAT_EQ(effect.light->intensity, 30.0f);
}

TEST(ParseRibbonEffect, AnEmptyFileIsAWhiteLine) {
    const core::Result<scene::RibbonEffectFile> parsed = scene::ParseRibbonEffect("{}");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->texture.empty());
    EXPECT_EQ(parsed->effect.shape, scene::RibbonShape::Line);
    EXPECT_FALSE(parsed->effect.light.has_value());
}

TEST(ParseRibbonEffect, ErrorsNameTheirKeyAndSayRibbon) {
    const std::string unknown = RibbonErrorOf(R"({"rate": 3})");
    EXPECT_NE(unknown.find("rate"), std::string::npos);
    EXPECT_NE(unknown.find("Ribbon"), std::string::npos);
    EXPECT_NE(RibbonErrorOf(R"({"shape": "spiral"})").find("shape"), std::string::npos);
    EXPECT_NE(RibbonErrorOf(R"({"tiling": 0})").find("tiling"), std::string::npos);
    EXPECT_NE(RibbonErrorOf(R"({"shape": "bolt", "generations": 11})").find("generations"), std::string::npos);
    EXPECT_NE(RibbonErrorOf(R"({"shape": "trail", "duration": 0})").find("duration"), std::string::npos);
    EXPECT_NE(RibbonErrorOf(R"({"light": {"brightness": 2}})").find("light.brightness"), std::string::npos);
}
