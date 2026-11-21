// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/EffectDocument.hpp>
#include <daidalos/scene/ImportSettings.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "AssetReferences.hpp"

using namespace daidalos;
using namespace editor;

namespace {

/// A small asset tree in a folder of its own: a level, a prefab, an effect and a sequence naming one sound and
/// one texture.
class AssetReferencesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // A folder per test: ctest runs them in parallel.
        m_root =
            std::filesystem::temp_directory_path() / (std::string("daidalos_asset_references_") +
                                                      ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root / "Factory/Sounds");
        std::filesystem::create_directories(m_root / "Factory/Effects");
        Write("Factory/Sounds/hum.wav", "RIFF");
        Write("Factory/Effects/spark.png", "png");
        Write("Factory/Effects/spark.effect.json", R"({"texture": "spark.png", "comment": "Sparks."})");
        Write("Factory/Sequences.sequence.json", "{}");
        scene::Level level;
        scene::LevelEntity hum{.id = 1, .name = "Hum"};
        hum.sound = scene::LevelSound{.clip = "Sounds/hum.wav"};
        level.entities.push_back(hum);
        scene::LevelEntity sparks{.id = 2, .name = "Sparks"};
        sparks.particles = scene::LevelParticles{.effect = "Effects/spark.effect.json"};
        level.entities.push_back(sparks);
        ASSERT_TRUE(scene::SaveLevelFile(m_root / "Factory/Factory.level.json", level).has_value());
        scene::Level prefab;
        scene::LevelEntity buzz{.id = 1, .name = "Buzz"};
        buzz.sound = scene::LevelSound{.clip = "../Sounds/hum.wav"};
        prefab.entities.push_back(buzz);
        std::filesystem::create_directories(m_root / "Factory/Prefabs");
        ASSERT_TRUE(scene::SaveLevelFile(m_root / "Factory/Prefabs/lamp.prefab.json", prefab).has_value());
    }
    void TearDown() override { std::filesystem::remove_all(m_root); }

    void Write(const std::string& path, const std::string& text) const {
        std::ofstream(m_root / path, std::ios::binary) << text;
    }
    [[nodiscard]] std::string Read(const std::string& path) const {
        std::ifstream file(m_root / path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::filesystem::path m_root;
};

} // namespace

TEST_F(AssetReferencesTest, FindsEveryFileThatNamesIt) {
    const std::vector<AssetReference> sound = FindReferences(m_root, "Factory/Sounds/hum.wav");
    ASSERT_EQ(sound.size(), 2U);
    EXPECT_EQ(sound[0].file, "Factory/Factory.level.json");
    EXPECT_EQ(sound[0].where, "Hum's sound");
    EXPECT_EQ(sound[1].file, "Factory/Prefabs/lamp.prefab.json");
    const std::vector<AssetReference> texture = FindReferences(m_root, "Factory/Effects/spark.png");
    ASSERT_EQ(texture.size(), 1U);
    EXPECT_EQ(texture[0].file, "Factory/Effects/spark.effect.json");
    EXPECT_TRUE(FindReferences(m_root, "Factory/nothing.png").empty());
}

TEST_F(AssetReferencesTest, MovingRewritesWhatNamesIt) {
    const core::Result<std::vector<std::string>> moved =
        MoveAsset(m_root, "Factory/Sounds/hum.wav", "Shared/Audio/hum_low.wav");
    ASSERT_TRUE(moved.has_value()) << core::Format(moved.error());
    EXPECT_EQ(moved->size(), 2U);
    EXPECT_TRUE(std::filesystem::exists(m_root / "Shared/Audio/hum_low.wav"));
    EXPECT_FALSE(std::filesystem::exists(m_root / "Factory/Sounds/hum.wav"));
    const core::Result<scene::Level> level = scene::LoadLevelFile(m_root / "Factory/Factory.level.json");
    ASSERT_TRUE(level.has_value());
    EXPECT_EQ(level->Find(1)->sound->clip, "../Shared/Audio/hum_low.wav");
    const core::Result<scene::Level> prefab = scene::LoadLevelFile(m_root / "Factory/Prefabs/lamp.prefab.json");
    EXPECT_EQ(prefab->Find(1)->sound->clip, "../../Shared/Audio/hum_low.wav");
    EXPECT_EQ(FindReferences(m_root, "Shared/Audio/hum_low.wav").size(), 2U);
}

TEST_F(AssetReferencesTest, AnEffectsTextureFollowsAndItsCommentStays) {
    const core::Result<std::vector<std::string>> moved =
        MoveAsset(m_root, "Factory/Effects/spark.png", "Factory/Effects/Images/spark.png");
    ASSERT_TRUE(moved.has_value()) << core::Format(moved.error());
    const core::Result<scene::EffectDocument> effect =
        scene::EffectDocument::Parse(Read("Factory/Effects/spark.effect.json"));
    ASSERT_TRUE(effect.has_value());
    EXPECT_EQ(effect->Text("texture"), "Images/spark.png");
    EXPECT_EQ(effect->Text("comment"), "Sparks.");
}

TEST_F(AssetReferencesTest, TheOpenLevelIsLeftToTheEditor) {
    const core::Result<std::vector<std::string>> moved =
        MoveAsset(m_root, "Factory/Sounds/hum.wav", "Factory/Sounds/drone.wav", "Factory/Factory.level.json");
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->size(), 1U);
    core::Result<scene::Level> level = scene::LoadLevelFile(m_root / "Factory/Factory.level.json");
    EXPECT_EQ(level->Find(1)->sound->clip, "Sounds/hum.wav");
    EXPECT_EQ(RepointLevel(*level, "Factory/Factory.level.json", "Factory/Sounds/hum.wav", "Factory/Sounds/drone.wav"),
              1U);
    EXPECT_EQ(level->Find(1)->sound->clip, "Sounds/drone.wav");
}

TEST_F(AssetReferencesTest, NothingMovesOverAFileOrFromNowhere) {
    EXPECT_FALSE(MoveAsset(m_root, "Factory/Sounds/hum.wav", "Factory/Effects/spark.png").has_value());
    EXPECT_FALSE(MoveAsset(m_root, "Factory/Sounds/none.wav", "Factory/Sounds/x.wav").has_value());
}

TEST(ImportSettingsTest, SettingsReadWriteAndScaleTheRoot) {
    const scene::ModelImportSettings settings{.scale = 0.01f, .levelCollision = true};
    const core::Result<scene::ModelImportSettings> read =
        scene::ParseImportSettings(scene::WriteImportSettings(settings));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, settings);
    EXPECT_FALSE(scene::ParseImportSettings(R"({"scale": 0})").has_value());
    EXPECT_FALSE(scene::ParseImportSettings(R"({"sclae": 1})").has_value());
    resources::ModelData model;
    model.nodes.push_back({.name = "root"});
    model.nodes[0].local.translation = glm::vec3(100.0f, 0.0f, 0.0f);
    model.nodes.push_back({.name = "child", .parent = 0u});
    model.nodes[1].local.translation = glm::vec3(50.0f, 0.0f, 0.0f);
    scene::ApplyImportSettings(model, settings);
    EXPECT_FLOAT_EQ(model.nodes[0].local.translation.x, 1.0f);
    EXPECT_FLOAT_EQ(model.nodes[0].local.scale.x, 0.01f);
    EXPECT_FLOAT_EQ(model.nodes[1].local.translation.x, 50.0f); // Scaled by its parent.
}
