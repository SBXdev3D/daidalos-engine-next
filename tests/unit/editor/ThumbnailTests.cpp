// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>

#include "Thumbnail.hpp"

using namespace daidalos;

namespace {

const std::filesystem::path kAssets = DAI_TEST_ASSET_DIR;

std::size_t Covered(const std::vector<std::uint8_t>& pixels) {
    std::size_t covered = 0;
    for (std::size_t index = 3; index < pixels.size(); index += 4) {
        covered += pixels[index] > 0 ? 1 : 0;
    }
    return covered;
}

TEST(Thumbnail, DrawsAModelOnATransparentBackground) {
    const core::Result<resources::ModelData> model = resources::LoadModelFile(kAssets / "lamp.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    const std::vector<std::uint8_t> pixels = editor::RenderClay(*model, 64);
    ASSERT_EQ(pixels.size(), 64U * 64U * 4U);
    const std::size_t covered = Covered(pixels);
    EXPECT_GT(covered, 64U * 64U / 20U) << "The model fills a good part of the picture";
    EXPECT_LT(covered, 64U * 64U) << "The background is transparent";
    EXPECT_EQ(pixels[3], 0U) << "A corner is background";
}

TEST(Thumbnail, AModelWithoutTrianglesIsEmpty) {
    const resources::ModelData empty;
    EXPECT_EQ(Covered(editor::RenderClay(empty, 16)), 0U);
}

TEST(Thumbnail, ShrinksTheCenterOfAnImage) {
    resources::ImageData image{.width = 4, .height = 2, .format = resources::PixelFormat::Rgba8Unorm};
    image.pixels.resize(4 * 2 * 4, std::byte{0});
    // The two middle columns are white: the square at the center.
    for (int y = 0; y < 2; ++y) {
        for (int x = 1; x < 3; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                image.pixels[static_cast<std::size_t>((y * 4 + x) * 4 + channel)] = std::byte{255};
            }
        }
    }
    const std::vector<std::uint8_t> pixels = editor::Shrink(image, 1);
    ASSERT_EQ(pixels.size(), 4U);
    EXPECT_EQ(pixels[0], 255U);

    image.format = resources::PixelFormat::Bc7Srgb;
    EXPECT_TRUE(editor::Shrink(image, 1).empty());
}

} // namespace
