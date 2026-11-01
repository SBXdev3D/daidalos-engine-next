// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/resources/ImageData.hpp>

#include <gtest/gtest.h>

#include <ktx.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

using namespace daidalos;
using resources::ImageData;
using resources::PixelFormat;
using resources::TextureUse;

namespace {

const std::filesystem::path kAssets = DAI_TEST_ASSET_DIR;

ImageData Rgba(std::uint32_t width, std::uint32_t height, PixelFormat format, std::vector<std::uint8_t> pixels) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.format = format;
    image.pixels.resize(pixels.size());
    std::memcpy(image.pixels.data(), pixels.data(), pixels.size());
    return image;
}

std::uint8_t Byte(const ImageData& image, std::size_t index) {
    return static_cast<std::uint8_t>(image.pixels[index]);
}

// Encodes a mid-gray 8x8 image to KTX2 in UASTC with the library itself, so that the tests need
// neither the command-line tools nor a binary file.
std::vector<std::byte> MakeUastcKtx2(bool srgb) {
    ktxTextureCreateInfo info{};
    info.vkFormat = srgb ? 43U : 37U; // VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM.
    info.baseWidth = 8;
    info.baseHeight = 8;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    info.generateMipmaps = KTX_FALSE;

    ktxTexture2* texture = nullptr;
    EXPECT_EQ(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture), KTX_SUCCESS);
    std::array<std::uint8_t, 8 * 8 * 4> pixels{};
    pixels.fill(128);
    EXPECT_EQ(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, pixels.data(), pixels.size()), KTX_SUCCESS);

    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.threadCount = 1;
    EXPECT_EQ(ktxTexture2_CompressBasisEx(texture, &params), KTX_SUCCESS);

    ktx_uint8_t* written = nullptr;
    ktx_size_t size = 0;
    EXPECT_EQ(ktxTexture_WriteToMemory(ktxTexture(texture), &written, &size), KTX_SUCCESS);
    const auto* first = reinterpret_cast<const std::byte*>(written);
    std::vector<std::byte> bytes(first, first + size);
    std::free(written);
    ktxTexture_Destroy(ktxTexture(texture));
    return bytes;
}

} // namespace

TEST(GenerateMips, ChainGoesDownToOnePixel) {
    ImageData image = Rgba(4, 2, PixelFormat::Rgba8Unorm, std::vector<std::uint8_t>(4 * 2 * 4, 200));
    image = resources::GenerateMips(std::move(image));
    EXPECT_EQ(image.mipLevels, 3U);
    EXPECT_EQ(image.pixels.size(), (8U + 2U + 1U) * 4U);
}

TEST(GenerateMips, AveragesTwoByTwoPixels) {
    const ImageData image = resources::GenerateMips(
        Rgba(2, 2, PixelFormat::Rgba8Unorm, {0, 0, 0, 0, 200, 0, 0, 0, 0, 0, 0, 0, 200, 0, 0, 255}));
    ASSERT_EQ(image.mipLevels, 2U);
    EXPECT_EQ(Byte(image, 16), 100);
    EXPECT_EQ(Byte(image, 19), 64);
}

TEST(GenerateMips, SrgbColorsAreAveragedInLinearSpaceButNotAlpha) {
    const ImageData image =
        resources::GenerateMips(Rgba(2, 1, PixelFormat::Rgba8Srgb, {0, 0, 0, 0, 255, 255, 255, 255}));
    // Half of full light is 188 in sRGB, not 128.
    EXPECT_NEAR(Byte(image, 8), 188, 1);
    EXPECT_NEAR(Byte(image, 11), 128, 1);
}

TEST(GenerateMips, OneDimensionalImagesAreSupported) {
    const ImageData image = resources::GenerateMips(Rgba(1, 2, PixelFormat::Rgba8Unorm, {100, 0, 0, 0, 200, 0, 0, 0}));
    ASSERT_EQ(image.mipLevels, 2U);
    EXPECT_EQ(Byte(image, 8), 150);
}

TEST(LoadImageFile, DecodesAPngWithAFullMipChain) {
    const core::Result<ImageData> image = resources::LoadImageFile(kAssets / "checker.png", TextureUse::Color);
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    EXPECT_EQ(image->width, 2U);
    EXPECT_EQ(image->height, 2U);
    EXPECT_EQ(image->mipLevels, 2U);
    EXPECT_EQ(image->format, PixelFormat::Rgba8Srgb);
    EXPECT_EQ(Byte(*image, 0), 255);
    EXPECT_EQ(Byte(*image, 6), 255);
}

TEST(LoadImageFile, DataTexturesAreLinear) {
    const core::Result<ImageData> image = resources::LoadImageFile(kAssets / "checker.png", TextureUse::Data);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->format, PixelFormat::Rgba8Unorm);
}

TEST(LoadImageFile, MissingFileIsNotFound) {
    const core::Result<ImageData> image = resources::LoadImageFile(kAssets / "no-such-image.png", TextureUse::Color);
    ASSERT_FALSE(image.has_value());
    EXPECT_EQ(image.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(image.error().message.find("no-such-image.png"), std::string::npos);
}

TEST(DecodeImage, InvalidDataIsAParseErrorThatNamesTheImage) {
    const std::array<std::byte, 16> garbage{};
    const core::Result<ImageData> image = resources::DecodeImage(garbage, TextureUse::Color, "garbage.png");
    ASSERT_FALSE(image.has_value());
    EXPECT_EQ(image.error().code, core::ErrorCode::ParseError);
    EXPECT_NE(image.error().message.find("garbage.png"), std::string::npos);
}

TEST(DecodeImage, TruncatedKtx2IsAParseError) {
    const std::vector<std::byte> bytes = MakeUastcKtx2(true);
    const core::Result<ImageData> image =
        resources::DecodeImage(std::span(bytes).first(32), TextureUse::Color, "truncated.ktx2");
    ASSERT_FALSE(image.has_value());
    EXPECT_EQ(image.error().code, core::ErrorCode::ParseError);
}

TEST(DecodeImage, UastcColorIsTranscodedToBc7) {
    const std::vector<std::byte> bytes = MakeUastcKtx2(true);
    const core::Result<ImageData> image = resources::DecodeImage(bytes, TextureUse::Color, "color.ktx2");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    EXPECT_EQ(image->format, PixelFormat::Bc7Srgb);
    EXPECT_EQ(image->width, 8U);
    EXPECT_EQ(image->mipLevels, 1U);
    EXPECT_EQ(image->pixels.size(), 4U * 16U); // Four blocks of 16 bytes.
}

TEST(DecodeImage, UastcNormalMapIsTranscodedToBc5) {
    const std::vector<std::byte> bytes = MakeUastcKtx2(false);
    const core::Result<ImageData> image = resources::DecodeImage(bytes, TextureUse::Normal, "normal.ktx2");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    EXPECT_EQ(image->format, PixelFormat::Bc5Unorm);
    EXPECT_EQ(image->pixels.size(), 4U * 16U);
}
