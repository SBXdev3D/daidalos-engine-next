// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/SaveGame.hpp>

#include <gtest/gtest.h>

#include <filesystem>

using namespace daidalos;

TEST(SaveGameTest, ValuesComeBackAsTheyWent) {
    scene::SaveData data;
    data.Put("player.health", 73.0);
    data.Put("player.flashlight", true);
    data.Put("player.name", "Ana \"the\" brave");
    data.Put("player.feet", glm::vec3(1.5f, -2.0f, 3.25f));
    data.Put("player.facing", glm::quat(0.5f, 0.5f, -0.5f, 0.5f));
    const core::Result<scene::SaveData> read = scene::SaveData::Parse(data.Write());
    ASSERT_TRUE(read.has_value()) << core::Format(read.error());
    EXPECT_EQ(*read, data);
    EXPECT_EQ(read->Number("player.health"), 73.0);
    EXPECT_EQ(read->Flag("player.flashlight"), true);
    EXPECT_EQ(read->Text("player.name"), "Ana \"the\" brave");
    EXPECT_EQ(read->Vector("player.feet"), glm::vec3(1.5f, -2.0f, 3.25f));
    EXPECT_EQ(read->Rotation("player.facing"), glm::quat(0.5f, 0.5f, -0.5f, 0.5f));
    // The wrong type, or nothing, is no value.
    EXPECT_FALSE(read->Number("player.flashlight").has_value());
    EXPECT_FALSE(read->Flag("player.mood").has_value());
    EXPECT_EQ(read->Keys("player.f"), (std::vector<std::string>{"player.facing", "player.feet", "player.flashlight"}));
}

TEST(SaveGameTest, WhatIsNotASaveIsAnError) {
    EXPECT_FALSE(scene::SaveData::Parse("").has_value());
    EXPECT_FALSE(scene::SaveData::Parse("hello\na = 1\n").has_value());
    const core::Result<scene::SaveData> broken = scene::SaveData::Parse("daidalos-save 1\nno equals here\n");
    ASSERT_FALSE(broken.has_value());
    EXPECT_NE(core::Format(broken.error()).find("line 2"), std::string::npos);
}

TEST(SaveGameTest, AFileIsWrittenWholeAndReadBack) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "daidalos-save-test" / "slot.save";
    scene::SaveData data;
    data.Put("kills", 4.0);
    ASSERT_TRUE(scene::WriteSave(file, data).has_value());
    data.Put("kills", 5.0);
    ASSERT_TRUE(scene::WriteSave(file, data).has_value());
    const core::Result<scene::SaveData> read = scene::ReadSave(file);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->Number("kills"), 5.0);
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".writing"));
    std::filesystem::remove_all(file.parent_path());
    EXPECT_FALSE(scene::ReadSave(file).has_value());
}
