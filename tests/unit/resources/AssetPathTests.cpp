// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/AssetPath.hpp>
#include <daidalos/resources/Cache.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace daidalos;
using resources::NormalizeAssetPath;
using resources::ResolveAssetPath;

TEST(NormalizeAssetPath, DifferentSpellingsGiveTheSameIdentity) {
    for (const char* spelling : {"models/crate.gltf", "models\\crate.gltf", "./models/crate.gltf",
                                 "models/../models/crate.gltf", "models//crate.gltf"}) {
        const core::Result<std::string> path = NormalizeAssetPath(spelling);
        ASSERT_TRUE(path.has_value()) << spelling;
        EXPECT_EQ(*path, "models/crate.gltf") << spelling;
    }
}

TEST(NormalizeAssetPath, KeepsTheCase) {
    EXPECT_EQ(NormalizeAssetPath("Models/Crate.GLTF").value(), "Models/Crate.GLTF");
}

TEST(NormalizeAssetPath, RejectsPathsThatAreNotUnderTheRoot) {
    for (const char* path :
         {"", ".", "/models/crate.gltf", "C:/models/crate.gltf", "../crate.gltf", "models/../../crate.gltf", "models/",
          "C:crate.gltf", "models/C:/crate.gltf", "crate.gltf:stream", "\\\\server\\share\\crate.gltf"}) {
        const core::Result<std::string> normalized = NormalizeAssetPath(path);
        ASSERT_FALSE(normalized.has_value()) << path;
        EXPECT_EQ(normalized.error().code, core::ErrorCode::InvalidArgument) << path;
    }
}

TEST(ResolveAssetPath, FindsAFile) {
    const core::Result<std::filesystem::path> path = ResolveAssetPath(DAI_TEST_ASSET_DIR, "quad.gltf");
    ASSERT_TRUE(path.has_value()) << core::Format(path.error());
    EXPECT_TRUE(std::filesystem::is_regular_file(*path));
}

TEST(ResolveAssetPath, MissingFileIsNotFoundAndNamesTheAsset) {
    const core::Result<std::filesystem::path> path = ResolveAssetPath(DAI_TEST_ASSET_DIR, "no-such-file.gltf");
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(path.error().message.find("no-such-file.gltf"), std::string::npos);
}

TEST(ResolveAssetPath, WrongCaseIsAnErrorOnEveryPlatform) {
    const core::Result<std::filesystem::path> path = ResolveAssetPath(DAI_TEST_ASSET_DIR, "QUAD.gltf");
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(path.error().message.find("quad.gltf"), std::string::npos);
}

TEST(Cache, FindsWhatWasInserted) {
    resources::Cache<int> cache;
    EXPECT_EQ(cache.Find("a"), nullptr);
    cache.Insert("a", 1);
    ASSERT_NE(cache.Find("a"), nullptr);
    EXPECT_EQ(*cache.Find("a"), 1);
    EXPECT_EQ(cache.Size(), 1U);
}

TEST(Cache, PointersSurviveLaterInsertions) {
    resources::Cache<int> cache;
    const int& first = cache.Insert("first", 1);
    for (int index = 0; index < 1000; ++index) {
        cache.Insert(std::to_string(index), index);
    }
    EXPECT_EQ(&first, cache.Find("first"));
}

TEST(Cache, ForEachVisitsEveryValue) {
    resources::Cache<int> cache;
    cache.Insert("a", 1);
    cache.Insert("b", 2);
    int sum = 0;
    cache.ForEach([&sum](int value) { sum += value; });
    EXPECT_EQ(sum, 3);
}
