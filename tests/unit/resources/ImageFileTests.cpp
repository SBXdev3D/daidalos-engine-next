// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/ImageData.hpp>
#include <daidalos/resources/ImageFile.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace daidalos;

namespace {

template <typename T>
T ReadAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::size_t Find(const std::vector<std::byte>& bytes, std::string_view text) {
    const auto* begin = reinterpret_cast<const char*>(bytes.data());
    const std::string_view all(begin, bytes.size());
    return all.find(text);
}

} // namespace

TEST(ImageFileTest, APngReadsBackAsItsPixels) {
    const std::vector<std::uint8_t> rgba{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 128, 10, 20, 30, 40};
    const core::Result<std::vector<std::byte>> png = resources::EncodePng(2, 2, rgba);
    ASSERT_TRUE(png.has_value()) << core::Format(png.error());
    const core::Result<resources::ImageData> image = resources::DecodeImage(*png, resources::TextureUse::Data, "png");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    ASSERT_EQ(image->width, 2U);
    ASSERT_EQ(image->height, 2U);
    for (std::size_t index = 0; index < rgba.size(); ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(image->pixels[index]), rgba[index]) << index;
    }
    EXPECT_FALSE(resources::EncodePng(2, 2, std::span(rgba).first(4)).has_value());
}

TEST(ImageFileTest, AnExrHasItsHeaderOffsetsAndScanlinesOfFloats) {
    // Two by two, each pixel's red its index, green 10 times it, blue -1.
    std::vector<float> rgb;
    for (int pixel = 0; pixel < 4; ++pixel) {
        rgb.insert(rgb.end(), {static_cast<float>(pixel), static_cast<float>(pixel) * 10.0f, -1.0f});
    }
    const core::Result<std::vector<std::byte>> exr = resources::EncodeExr(2, 2, rgb);
    ASSERT_TRUE(exr.has_value()) << core::Format(exr.error());
    EXPECT_EQ(ReadAt<std::uint32_t>(*exr, 0), 20000630U);
    EXPECT_EQ(ReadAt<std::uint32_t>(*exr, 4), 2U);
    for (const std::string_view attribute : {"channels", "compression", "dataWindow", "displayWindow", "lineOrder",
                                             "pixelAspectRatio", "screenWindowCenter", "screenWindowWidth"}) {
        EXPECT_NE(Find(*exr, attribute), std::string_view::npos) << attribute;
    }
    // The header ends with an empty name; then two offsets, then two scanlines of 8 + 2 * 3 * 4 bytes.
    constexpr std::size_t kLine = 8 + (2 * 3 * 4);
    ASSERT_GT(exr->size(), (2 * kLine) + 16);
    const std::size_t table = exr->size() - (2 * kLine) - 16;
    EXPECT_EQ(static_cast<char>((*exr)[table - 1]), '\0');
    const auto first = ReadAt<std::uint64_t>(*exr, table);
    const auto second = ReadAt<std::uint64_t>(*exr, table + 8);
    EXPECT_EQ(first, table + 16);
    EXPECT_EQ(second, first + kLine);
    // The second line: y, its size, then B, G and R of its two pixels, the third and fourth.
    EXPECT_EQ(ReadAt<std::int32_t>(*exr, second), 1);
    EXPECT_EQ(ReadAt<std::uint32_t>(*exr, second + 4), 24U);
    EXPECT_FLOAT_EQ(ReadAt<float>(*exr, second + 8), -1.0f);
    EXPECT_FLOAT_EQ(ReadAt<float>(*exr, second + 16), 20.0f);
    EXPECT_FLOAT_EQ(ReadAt<float>(*exr, second + 20), 30.0f);
    EXPECT_FLOAT_EQ(ReadAt<float>(*exr, second + 24), 2.0f);
    EXPECT_FLOAT_EQ(ReadAt<float>(*exr, second + 28), 3.0f);
    EXPECT_FALSE(resources::EncodeExr(2, 2, std::span(rgb).first(3)).has_value());
}

TEST(ImageFileTest, AJpegReadsBackCloseToItsPixels) {
    // Eight by eight of one color, which JPEG keeps nearly as it is.
    std::vector<std::uint8_t> rgba;
    for (int pixel = 0; pixel < 64; ++pixel) {
        rgba.insert(rgba.end(), {200, 100, 50, 255});
    }
    const core::Result<std::vector<std::byte>> jpeg = resources::EncodeJpeg(8, 8, rgba, 95);
    ASSERT_TRUE(jpeg.has_value()) << core::Format(jpeg.error());
    const core::Result<resources::ImageData> image = resources::DecodeImage(*jpeg, resources::TextureUse::Data, "jpg");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    ASSERT_EQ(image->width, 8U);
    ASSERT_EQ(image->height, 8U);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(static_cast<int>(static_cast<std::uint8_t>(image->pixels[(27 * 4) + channel])), rgba[channel], 4)
            << channel;
    }
    EXPECT_FALSE(resources::EncodeJpeg(8, 8, std::span(rgba).first(4), 95).has_value());
}
