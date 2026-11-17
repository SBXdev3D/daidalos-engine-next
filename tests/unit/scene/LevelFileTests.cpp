// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Level.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace daidalos;
using scene::Level;
using scene::LevelEntity;
using scene::ParseLevel;
using scene::WriteLevel;

namespace {

std::string ErrorOf(std::string_view json) {
    const core::Result<Level> parsed = ParseLevel(json);
    EXPECT_FALSE(parsed.has_value());
    if (parsed.has_value()) {
        return {};
    }
    EXPECT_EQ(parsed.error().code, core::ErrorCode::ParseError);
    return parsed.error().message;
}

Level Everything() {
    Level level;
    level.environment = {.skyFaces = "Sky/sky1",
                         .skyIntensity = 9.6f,
                         .skyYaw = 180.0f,
                         .fogDensity = 0.01f,
                         .fogAlbedo = {0.8f, 0.82f, 0.85f},
                         .fogAnisotropy = 0.1f,
                         .ev100 = 3.0f,
                         .autoExposure = true,
                         .exposureMin = 0.5f,
                         .exposureMax = 2.5f,
                         .wind = {1.0f, 0.0f, 0.5f}};
    LevelEntity factory{.id = 1, .name = "Factory", .model = "Factory.gltf"};
    LevelEntity alarm{.id = 2, .name = "Alarm \"west\"", .model = "Props/alarm.gltf"};
    alarm.transform.translation = {-10.5f, 8.4f, -47.25f};
    alarm.transform.rotation = glm::angleAxis(0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
    alarm.game = scene::Tags{.kind = "alarm", .values = {{"rotation_speed", "-60"}}};
    LevelEntity beam{.id = 3, .name = "Beam", .parent = 2};
    beam.light = scene::Light{.type = renderer::LightType::Spot,
                              .color = {1.0f, 0.0f, 0.0f},
                              .intensity = 965.0f,
                              .range = 28.0f,
                              .innerAngle = glm::radians(16.0f),
                              .outerAngle = glm::radians(20.0f)};
    LevelEntity steam{.id = 4, .parent = 5};
    steam.transform.scale = {1.6f, 1.6f, 4.0f};
    steam.fog = scene::FogVolume{.density = 0.8f, .noise = 0.8f, .noiseVelocity = {0.0f, 0.1f, -0.5f}};
    LevelEntity leak{.id = 5, .name = "Leak"};
    leak.particles = scene::LevelParticles{.effect = "Effects/smoke_leak.effect.json",
                                           .burst = scene::ParticleBurst{.count = 3, .interval = 2.0f},
                                           .surface = scene::LevelNodeRef{.entity = 1, .node = "pit"}};
    leak.wind = scene::WindVolume{.velocity = {0.0f, 0.0f, -1.0f}, .gust = 0.5f};
    leak.attractor = scene::LevelAttractor{.effect = "Effects/flies.effect.json", .strength = 3.0f, .vortex = 2.0f};
    leak.weather = scene::LevelWeather{.effect = "Effects/rain.effect.json", .rate = 60.0f};
    leak.ribbon = scene::LevelRibbon{.effect = "Effects/arc.ribbon.json", .target = 2};
    leak.lightning =
        scene::LevelLightning{.bolt = "Effects/lightning.ribbon.json", .light = 3, .interval = {4.0f, 9.0f}};
    level.entities = {factory, alarm, beam, steam, leak};
    return level;
}

} // namespace

TEST(LevelFileTest, WritesAndReadsBackTheSameLevel) {
    const Level level = Everything();
    const core::Result<Level> parsed = ParseLevel(WriteLevel(level));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->environment, level.environment);
    ASSERT_EQ(parsed->entities.size(), level.entities.size());
    for (std::size_t index = 0; index < level.entities.size(); ++index) {
        // Angles go through degrees: compare them close, then the rest exactly.
        LevelEntity read = parsed->entities[index];
        const LevelEntity& written = level.entities[index];
        if (read.light && written.light) {
            EXPECT_NEAR(read.light->innerAngle, written.light->innerAngle, 1e-6f);
            EXPECT_NEAR(read.light->outerAngle, written.light->outerAngle, 1e-6f);
            read.light->innerAngle = written.light->innerAngle;
            read.light->outerAngle = written.light->outerAngle;
        }
        EXPECT_NEAR(glm::dot(read.transform.rotation, written.transform.rotation), 1.0f, 1e-6f);
        read.transform.rotation = written.transform.rotation;
        EXPECT_EQ(read, written) << "entity " << written.id;
    }
}

TEST(LevelFileTest, WritingTwiceGivesTheSameText) {
    const std::string first = WriteLevel(Everything());
    const core::Result<Level> parsed = ParseLevel(first);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(WriteLevel(*parsed), first);
}

TEST(LevelFileTest, LeavesDefaultsOut) {
    Level level;
    level.entities.push_back(LevelEntity{.id = 1});
    const std::string text = WriteLevel(level);
    EXPECT_EQ(text.find("transform"), std::string::npos) << text;
    EXPECT_EQ(text.find("environment\": {}"), text.find("environment")) << text;
}

TEST(LevelFileTest, ReadsAnglesInDegrees) {
    const core::Result<Level> parsed = ParseLevel(R"({"format": "daidalos-level", "version": 1, "entities": [
        {"id": 1, "light": {"type": "spot", "outer_angle": 45}}]})");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_NEAR(parsed->entities[0].light->outerAngle, glm::radians(45.0f), 1e-6f);
}

TEST(LevelFileTest, ReadsGameTags) {
    const core::Result<Level> parsed = ParseLevel(R"({"format": "daidalos-level", "version": 1, "entities": [
        {"id": 1, "game": {"kind": "zombie", "patrol": "1:0:0|2:0:0"}}]})");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    ASSERT_TRUE(parsed->entities[0].game.has_value());
    EXPECT_EQ(parsed->entities[0].game->kind, "zombie");
    EXPECT_EQ(parsed->entities[0].game->Value("patrol"), "1:0:0|2:0:0");
}

TEST(LevelFileTest, RefusesWhatItDoesNotKnow) {
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 1, "entites": []})").find("entites"),
              std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [
        {"id": 1, "light": {"intensty": 3}}]})")
                  .find("entities[0].light.intensty"),
              std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 2})").find("version"), std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "something", "version": 1})").find("format"), std::string::npos);
    EXPECT_NE(ErrorOf("[1, 2").find("JSON"), std::string::npos);
}

TEST(LevelFileTest, RefusesBadIds) {
    EXPECT_NE(
        ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [{"id": 1}, {"id": 1}]})").find("twice"),
        std::string::npos);
    EXPECT_NE(
        ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [{"id": 1, "parent": 7}]})").find("parent 7"),
        std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [
        {"id": 1, "parent": 2}, {"id": 2, "parent": 1}]})")
                  .find("ancestor"),
              std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [{"id": 0}]})").find("id"),
              std::string::npos);
    EXPECT_NE(ErrorOf(R"({"format": "daidalos-level", "version": 1, "entities": [{"id": 1.5}]})").find("id"),
              std::string::npos);
}

TEST(LevelFileTest, ParsesTagsFromNodeNames) {
    const scene::Tags tags = scene::ParseTags("zombie;health=50;model=zombie.gltf");
    EXPECT_EQ(tags.kind, "zombie");
    EXPECT_EQ(tags.Value("health"), "50");
    EXPECT_EQ(tags.Value("model"), "zombie.gltf");
    EXPECT_FALSE(tags.Has("patrol"));
    EXPECT_EQ(scene::ParseTags("level").kind, "level");
    EXPECT_TRUE(scene::ParseTags("level").values.empty());
}

TEST(LevelFileTest, ListsEachModelOnce) {
    Level level = Everything();
    level.entities.push_back(LevelEntity{.id = 9, .model = "Props/alarm.gltf"});
    const std::vector<std::string> models = scene::LevelModels(level, "Factory/Factory.level.json");
    EXPECT_EQ(models, (std::vector<std::string>{"Factory/Factory.gltf", "Factory/Props/alarm.gltf"}));
}

TEST(LevelFileTest, MakesALevelOfABareModel) {
    const Level level = scene::LevelOfModel("Lantern/Lantern.gltf");
    ASSERT_EQ(level.entities.size(), 1u);
    EXPECT_EQ(level.entities[0].model, "Lantern.gltf");
    EXPECT_EQ(scene::LevelModels(level, "Lantern/Lantern.gltf"), (std::vector<std::string>{"Lantern/Lantern.gltf"}));
}

TEST(LevelFileTest, RelocateKeepsPathsOnTheSameFiles) {
    Level level;
    level.environment.skyFaces = "Sky/sky1";
    LevelEntity entity{.id = 1, .model = "Factory.gltf"};
    entity.particles = scene::LevelParticles{.effect = "Effects/dust.effect.json"};
    level.entities.push_back(entity);

    Level moved = level;
    scene::Relocate(moved, "Factory/Factory.level.json", "Levels/Test/Copy.level.json");
    EXPECT_EQ(moved.environment.skyFaces, "../../Factory/Sky/sky1");
    EXPECT_EQ(moved.entities[0].model, "../../Factory/Factory.gltf");
    EXPECT_EQ(moved.entities[0].particles->effect, "../../Factory/Effects/dust.effect.json");

    scene::Relocate(moved, "Levels/Test/Copy.level.json", "Root.level.json");
    EXPECT_EQ(moved.entities[0].model, "Factory/Factory.gltf");

    Level same = level;
    scene::Relocate(same, "Factory/Factory.level.json", "Factory/Other.level.json");
    EXPECT_EQ(same, level);
}
