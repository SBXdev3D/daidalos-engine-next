// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/resources/FontData.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

using namespace daidalos;
using resources::FontData;
using resources::NextCodepoint;

namespace {

const std::filesystem::path kFont = DAI_TEST_FONT;

// Baked once: every test reads the same font.
const FontData& Rajdhani() {
    static const std::optional<FontData> font = [] {
        core::Result<FontData> baked = resources::LoadFontFile(kFont);
        return baked ? std::optional<FontData>(std::move(*baked)) : std::nullopt;
    }();
    if (!font.has_value()) {
        ADD_FAILURE() << "Cannot bake " << kFont.string();
        static const FontData empty;
        return empty;
    }
    return *font;
}

std::uint8_t Pixel(const FontData& font, std::size_t x, std::size_t y) {
    return static_cast<std::uint8_t>(font.atlas[(y * font.atlasWidth) + x]);
}

char32_t Decode(std::string_view text) {
    std::size_t offset = 0;
    const char32_t codepoint = NextCodepoint(text, offset);
    EXPECT_GT(offset, 0U);
    return codepoint;
}

} // namespace

TEST(FontData, BakesMetricsThatSpanTheSize) {
    const FontData& font = Rajdhani();
    EXPECT_FLOAT_EQ(font.size, 48.0f);
    EXPECT_GT(font.ascent, 0.0f);
    EXPECT_LT(font.descent, 0.0f);
    EXPECT_NEAR(font.ascent - font.descent, font.size, 0.01f);
    EXPECT_FLOAT_EQ(font.distanceRange, 6.0f);
    EXPECT_EQ(font.atlas.size(), static_cast<std::size_t>(font.atlasWidth) * font.atlasHeight);
}

TEST(FontData, HasAsciiAndFrenchCharactersInOrder) {
    const FontData& font = Rajdhani();
    for (const char32_t codepoint : {U'A', U'z', U'0', U'%', U'/', U'\u00E9', U'\u00E7', U'\u00C0'}) {
        EXPECT_NE(font.Find(codepoint), nullptr) << static_cast<std::uint32_t>(codepoint);
    }
    EXPECT_TRUE(std::is_sorted(font.glyphs.begin(), font.glyphs.end(),
                               [](const auto& a, const auto& b) { return a.codepoint < b.codepoint; }));
    // Not baked, so the fallback is the question mark.
    EXPECT_EQ(font.Find(U'\u4E2D'), nullptr);
    ASSERT_NE(font.FindOrFallback(U'\u4E2D'), nullptr);
    EXPECT_EQ(font.FindOrFallback(U'\u4E2D')->codepoint, U'?');
}

TEST(FontData, SpaceAdvancesWithoutAQuad) {
    const resources::GlyphData* space = Rajdhani().Find(U' ');
    ASSERT_NE(space, nullptr);
    EXPECT_GT(space->advance, 0.0f);
    EXPECT_EQ(space->width, 0.0f);
}

TEST(FontData, DistanceFieldIsInsideInTheMiddleOfAStrokeAndOutsideAtTheMargin) {
    const FontData& font = Rajdhani();
    // A vertical bar: its middle column is inside the stroke from top to bottom.
    const resources::GlyphData* bar = font.Find(U'|');
    ASSERT_NE(bar, nullptr);
    const auto left = static_cast<std::size_t>(bar->u0 * static_cast<float>(font.atlasWidth));
    const auto top = static_cast<std::size_t>(bar->v0 * static_cast<float>(font.atlasHeight));
    const auto width = static_cast<std::size_t>(bar->width);
    const auto height = static_cast<std::size_t>(bar->height);
    EXPECT_GT(Pixel(font, left + (width / 2), top + (height / 2)), 128U);
    EXPECT_EQ(Pixel(font, left, top), 0U);
    EXPECT_LT(Pixel(font, left + width - 1, top + height - 1), 128U);
}

TEST(FontData, KerningIsFoundByPair) {
    // Rajdhani has no kerning at all, so the pairs are made up.
    FontData font;
    font.kerning = {{U'A', U'V', -3.0f}, {U'A', U'W', -2.0f}, {U'T', U'o', -4.0f}};
    EXPECT_FLOAT_EQ(font.Kerning(U'A', U'W'), -2.0f);
    EXPECT_FLOAT_EQ(font.Kerning(U'T', U'o'), -4.0f);
    EXPECT_FLOAT_EQ(font.Kerning(U'V', U'A'), 0.0f);
    EXPECT_FLOAT_EQ(font.Kerning(U'Z', U'Z'), 0.0f);
    EXPECT_TRUE(Rajdhani().kerning.empty());
}

TEST(FontData, InvalidDataIsAParseError) {
    const std::array<std::byte, 16> garbage{};
    const core::Result<FontData> font = resources::BakeFont(garbage, {}, "garbage");
    ASSERT_FALSE(font.has_value());
    EXPECT_EQ(font.error().code, core::ErrorCode::ParseError);
    EXPECT_NE(font.error().message.find("garbage"), std::string::npos);
    EXPECT_FALSE(resources::BakeFont({}, {}, "empty").has_value());
}

TEST(FontData, MissingFileIsNotFound) {
    const core::Result<FontData> font = resources::LoadFontFile("no/such/font.ttf");
    ASSERT_FALSE(font.has_value());
    EXPECT_EQ(font.error().code, core::ErrorCode::NotFound);
}

TEST(NextCodepoint, DecodesEachLengthOfUtf8) {
    EXPECT_EQ(Decode("A"), U'A');
    EXPECT_EQ(Decode("\xC3\xA9"), U'\u00E9');
    EXPECT_EQ(Decode("\xE2\x82\xAC"), U'\u20AC');
    EXPECT_EQ(Decode("\xF0\x9F\x98\x80"), U'\U0001F600');

    std::size_t offset = 0;
    const std::string_view text = "a\xC3\xA9z";
    EXPECT_EQ(NextCodepoint(text, offset), U'a');
    EXPECT_EQ(NextCodepoint(text, offset), U'\u00E9');
    EXPECT_EQ(NextCodepoint(text, offset), U'z');
    EXPECT_EQ(offset, text.size());
}

TEST(NextCodepoint, InvalidBytesAreReplacedOneAtATime) {
    for (const std::string_view invalid : {"\x80", "\xFF", "\xC3", "\xC3\x41", "\xC0\xAF", "\xED\xA0\x80"}) {
        std::size_t offset = 0;
        EXPECT_EQ(NextCodepoint(invalid, offset), U'\uFFFD');
        EXPECT_EQ(offset, 1U);
    }
}
