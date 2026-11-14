// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

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

namespace {

ImageData Solid(std::uint32_t size, PixelFormat format, std::array<std::uint8_t, 4> color) {
    std::vector<std::uint8_t> pixels(std::size_t{size} * size * 4);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = color[index % 4];
    }
    return resources::GenerateMips(Rgba(size, size, format, std::move(pixels)));
}

// The value of the first texel of a BC4 block, half of a BC5 block.
std::uint8_t FirstBc4Texel(const ImageData& image, std::size_t blockOffset) {
    const int red0 = Byte(image, blockOffset);
    const int red1 = Byte(image, blockOffset + 1);
    const int index = Byte(image, blockOffset + 2) & 0x7;
    if (index < 2) {
        return static_cast<std::uint8_t>(index == 0 ? red0 : red1);
    }
    if (red0 > red1) {
        return static_cast<std::uint8_t>(((8 - index) * red0 + (index - 1) * red1) / 7);
    }
    if (index >= 6) {
        return (index == 6) ? 0 : 255;
    }
    return static_cast<std::uint8_t>(((6 - index) * red0 + (index - 1) * red1) / 5);
}

} // namespace

TEST(EncodeKtx2, ColorsComeBackAsBc7SrgbWithTheirMipLevels) {
    const core::Result<std::vector<std::byte>> bytes =
        resources::EncodeKtx2(Solid(16, PixelFormat::Rgba8Srgb, {200, 100, 50, 255}), TextureUse::Color);
    ASSERT_TRUE(bytes.has_value()) << core::Format(bytes.error());
    const core::Result<ImageData> image = resources::DecodeImage(*bytes, TextureUse::Color, "color.ktx2");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    EXPECT_EQ(image->format, PixelFormat::Bc7Srgb);
    EXPECT_EQ(image->width, 16U);
    EXPECT_EQ(image->mipLevels, 5U);
    // 16x16, 8x8 and 4x4 are 16, 4 and 1 blocks; 2x2 and 1x1 take a block each.
    EXPECT_EQ(image->pixels.size(), (16U + 4U + 1U + 1U + 1U) * 16U);
}

TEST(EncodeKtx2, NormalMapsKeepTheirRedAndGreenChannels) {
    const core::Result<std::vector<std::byte>> bytes =
        resources::EncodeKtx2(Solid(8, PixelFormat::Rgba8Unorm, {200, 60, 255, 255}), TextureUse::Normal);
    ASSERT_TRUE(bytes.has_value()) << core::Format(bytes.error());
    const core::Result<ImageData> image = resources::DecodeImage(*bytes, TextureUse::Normal, "normal.ktx2");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    ASSERT_EQ(image->format, PixelFormat::Bc5Unorm);
    EXPECT_NEAR(FirstBc4Texel(*image, 0), 200, 3);
    EXPECT_NEAR(FirstBc4Texel(*image, 8), 60, 3);
}

TEST(EncodeKtx2, ColorsMustBeSrgbAndOtherTexturesLinear) {
    const ImageData linear = Solid(4, PixelFormat::Rgba8Unorm, {0, 0, 0, 255});
    const core::Result<std::vector<std::byte>> bytes = resources::EncodeKtx2(linear, TextureUse::Color);
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(resources::EncodeKtx2(Solid(4, PixelFormat::Rgba8Srgb, {0, 0, 0, 255}), TextureUse::Data));
}

TEST(EncodeKtx2, MissingMipLevelsAreAnInvalidArgument) {
    ImageData image = Solid(4, PixelFormat::Rgba8Unorm, {0, 0, 0, 255});
    image.pixels.resize(4 * 4 * 4);
    const core::Result<std::vector<std::byte>> bytes = resources::EncodeKtx2(image, TextureUse::Data);
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, core::ErrorCode::InvalidArgument);
}
