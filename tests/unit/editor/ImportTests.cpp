// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Import.hpp"

using namespace daidalos;
using editor::AssetKind;

namespace {

const std::filesystem::path kAssets = DAI_TEST_ASSET_DIR;

class ImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_directory =
            std::filesystem::temp_directory_path() /
            ("daidalos-import-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory / "source");
        std::filesystem::create_directories(m_directory / "assets");
    }
    void TearDown() override { std::filesystem::remove_all(m_directory); }

    void Touch(const std::filesystem::path& file) const {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "x";
    }

    std::filesystem::path m_directory;
};

TEST(AssetKinds, AreKnownByTheirNames) {
    EXPECT_EQ(editor::KindOf("crate.gltf"), AssetKind::Model);
    EXPECT_EQ(editor::KindOf("Crate.GLB"), AssetKind::Model);
    EXPECT_EQ(editor::KindOf("fire.effect.json"), AssetKind::Effect);
    EXPECT_EQ(editor::KindOf("arc.ribbon.json"), AssetKind::Ribbon);
    EXPECT_EQ(editor::KindOf("Factory.level.json"), AssetKind::Level);
    EXPECT_EQ(editor::KindOf("Factory.play.level.json"), AssetKind::Other);
    EXPECT_EQ(editor::KindOf("sky1_front.png"), AssetKind::Sky);
    EXPECT_EQ(editor::KindOf("brick.png"), AssetKind::Other);
    EXPECT_EQ(editor::KindOf("alarm_beams.prefab.json"), AssetKind::Prefab);
    EXPECT_EQ(editor::KindOf("Factory/Decals/grime.png"), AssetKind::Decal);
    EXPECT_EQ(editor::KindOf("Factory/Decals/bullet_hole_normal.png"), AssetKind::Other);
    EXPECT_EQ(editor::SkyPrefix("night_sky_down.png"), "night_sky");
    EXPECT_FALSE(editor::SkyPrefix("_down.png").has_value());
    EXPECT_FALSE(editor::SkyPrefix("brick.png").has_value());
}

TEST_F(ImportTest, AModelComesWithTheFilesItReads) {
    std::filesystem::copy_file(kAssets / "quad.gltf", m_directory / "source/quad.gltf");
    std::filesystem::copy_file(kAssets / "checker.png", m_directory / "source/checker.png");

    const core::Result<std::filesystem::path> model =
        editor::ImportModel(m_directory / "source/quad.gltf", m_directory / "assets");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    EXPECT_EQ(*model, m_directory / "assets/quad/quad.gltf") << "A model with files gets a folder";
    EXPECT_TRUE(std::filesystem::exists(m_directory / "assets/quad/checker.png"));

    const core::Result<std::filesystem::path> again =
        editor::ImportModel(m_directory / "source/quad.gltf", m_directory / "assets");
    EXPECT_FALSE(again.has_value()) << "Nothing is replaced";
}

TEST_F(ImportTest, AModelOnItsOwnStaysInTheFolder) {
    std::filesystem::copy_file(kAssets / "lamp.gltf", m_directory / "source/lamp.gltf");
    const core::Result<std::filesystem::path> model =
        editor::ImportModel(m_directory / "source/lamp.gltf", m_directory / "assets");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    EXPECT_EQ(*model, m_directory / "assets/lamp.gltf");
}

TEST_F(ImportTest, AModelWithoutItsFilesIsRefused) {
    std::filesystem::copy_file(kAssets / "quad.gltf", m_directory / "source/quad.gltf");
    EXPECT_FALSE(editor::ImportModel(m_directory / "source/quad.gltf", m_directory / "assets").has_value());
    EXPECT_FALSE(std::filesystem::exists(m_directory / "assets/quad/quad.gltf"));
}

TEST_F(ImportTest, ASkyComesWithItsSixFaces) {
    for (const char* face : {"_right", "_left", "_up", "_down", "_front", "_back"}) {
        Touch(m_directory / "source" / (std::string("night") + face + ".png"));
    }
    const core::Result<std::filesystem::path> sky =
        editor::ImportSky(m_directory / "source/night_up.png", m_directory / "assets/Sky");
    ASSERT_TRUE(sky.has_value()) << core::Format(sky.error());
    EXPECT_EQ(*sky, m_directory / "assets/Sky/night");
    EXPECT_TRUE(std::filesystem::exists(m_directory / "assets/Sky/night_back.png"));

    const std::vector<editor::AssetEntry> listed = editor::ListFolder(m_directory / "assets", "Sky");
    ASSERT_EQ(listed.size(), 1U) << "One tile for the six faces";
    EXPECT_EQ(listed[0].kind, AssetKind::Sky);
    EXPECT_EQ(listed[0].path, "Sky/night");
}

TEST_F(ImportTest, ASkyWithAFaceMissingIsRefused) {
    Touch(m_directory / "source/night_front.png");
    EXPECT_FALSE(editor::ImportSky(m_directory / "source/night_front.png", m_directory / "assets").has_value());
}

TEST_F(ImportTest, FoldersComeFirstAndOtherFilesAreLeftOut) {
    Touch(m_directory / "assets/b.gltf");
    Touch(m_directory / "assets/a.effect.json");
    Touch(m_directory / "assets/texture.png");
    std::filesystem::create_directories(m_directory / "assets/Zeta");
    const std::vector<editor::AssetEntry> listed = editor::ListFolder(m_directory / "assets", "");
    ASSERT_EQ(listed.size(), 3U);
    EXPECT_EQ(listed[0].kind, AssetKind::Folder);
    EXPECT_EQ(listed[0].path, "Zeta");
    EXPECT_EQ(listed[1].name, "a.effect.json");
    EXPECT_EQ(listed[2].name, "b.gltf");
}

} // namespace
