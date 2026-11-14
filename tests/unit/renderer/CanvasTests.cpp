// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ui/Canvas.hpp>

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <algorithm>

using namespace daidalos;
using ui::Canvas;

namespace {

/// A font of three square glyphs, sized like a bake at 10 pixels: 'A' and '?' are 8 units wide with
/// an advance of 10, and a space advances 5. "AA" kerns by -2.
ui::Font TestFont() {
    ui::Font font;
    resources::FontData& data = font.data;
    data.size = 10.0f;
    data.ascent = 8.0f;
    data.descent = -2.0f;
    data.lineGap = 2.0f;
    const auto glyph = [](char32_t codepoint, float advance, float width) {
        return resources::GlyphData{.codepoint = codepoint,
                                    .advance = advance,
                                    .left = 1.0f,
                                    .top = -8.0f,
                                    .width = width,
                                    .height = width > 0.0f ? 10.0f : 0.0f,
                                    .u0 = 0.0f,
                                    .v0 = 0.0f,
                                    .u1 = 0.5f,
                                    .v1 = 0.5f};
    };
    data.glyphs = {glyph(U' ', 5.0f, 0.0f), glyph(U'?', 10.0f, 8.0f), glyph(U'A', 10.0f, 8.0f)};
    data.kerning = {{U'A', U'A', -2.0f}};
    return font;
}

rhi::TextureHandle Texture(std::uint32_t index) {
    return rhi::TextureHandle{index, 1};
}

Canvas Begun(std::uint32_t width = 1920, std::uint32_t height = 1080) {
    Canvas canvas;
    canvas.Begin({width, height});
    return canvas;
}

} // namespace

TEST(Canvas, IsTheReferenceHeightAndFollowsTheAspectRatio) {
    const Canvas canvas = Begun(1280, 720);
    EXPECT_FLOAT_EQ(canvas.Size().y, ui::kReferenceHeight);
    EXPECT_FLOAT_EQ(canvas.Size().x, 1920.0f);
    EXPECT_FLOAT_EQ(canvas.PixelsPerUnit(), 720.0f / 1080.0f);

    EXPECT_FLOAT_EQ(Begun(1080, 1080).Size().x, 1080.0f);
    EXPECT_EQ(Begun(0, 0).Size(), glm::vec2(0.0f));
}

TEST(Canvas, BeginClearsThePreviousFrame) {
    Canvas canvas = Begun();
    canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, glm::vec4(1.0f));
    canvas.Begin({1920, 1080});
    EXPECT_TRUE(canvas.Vertices().empty());
    EXPECT_TRUE(canvas.Indices().empty());
    EXPECT_TRUE(canvas.Commands().empty());
}

TEST(Canvas, RectangleIsTwoTrianglesInItsColor) {
    Canvas canvas = Begun();
    canvas.FillRect({{10.0f, 20.0f}, {30.0f, 40.0f}}, {1.0f, 0.0f, 0.0f, 1.0f});
    ASSERT_EQ(canvas.Vertices().size(), 4U);
    ASSERT_EQ(canvas.Commands().size(), 1U);
    EXPECT_EQ(canvas.Commands()[0].shading, ui::Shading::Solid);
    EXPECT_EQ(canvas.Commands()[0].indexCount, 6U);
    EXPECT_EQ(canvas.Vertices()[0].position, glm::vec2(10.0f, 20.0f));
    EXPECT_EQ(canvas.Vertices()[2].position, glm::vec2(40.0f, 60.0f));
    EXPECT_EQ(canvas.Vertices()[0].color, 0xFF0000FFU);
}

TEST(Canvas, TransparentDrawsAreSkipped) {
    Canvas canvas = Begun();
    canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1.0f, 1.0f, 1.0f, 0.0f});
    canvas.DrawImage(Texture(1), {{0.0f, 0.0f}, {10.0f, 10.0f}}, {.color = glm::vec4(0.0f)});
    EXPECT_TRUE(canvas.Commands().empty());
}

TEST(Canvas, ConsecutiveDrawsOfOneTextureShareACommand) {
    Canvas canvas = Begun();
    canvas.DrawImage(Texture(1), {{0.0f, 0.0f}, {10.0f, 10.0f}});
    canvas.DrawImage(Texture(1), {{20.0f, 0.0f}, {10.0f, 10.0f}});
    canvas.DrawImage(Texture(2), {{40.0f, 0.0f}, {10.0f, 10.0f}});
    canvas.FillRect({{0.0f, 0.0f}, {1.0f, 1.0f}}, glm::vec4(1.0f));
    canvas.DrawImage(Texture(1), {{0.0f, 0.0f}, {10.0f, 10.0f}});
    ASSERT_EQ(canvas.Commands().size(), 4U);
    EXPECT_EQ(canvas.Commands()[0].indexCount, 12U);
    EXPECT_EQ(canvas.Commands()[1].texture, Texture(2));
    EXPECT_EQ(canvas.Commands()[1].firstIndex, 12U);
    EXPECT_EQ(canvas.Commands()[2].shading, ui::Shading::Solid);
    EXPECT_EQ(canvas.Commands()[3].firstIndex, 24U);
}

TEST(Canvas, ImageTakesThePartOfTheTextureAsked) {
    Canvas canvas = Begun();
    canvas.DrawImage(Texture(1), {{0.0f, 0.0f}, {10.0f, 10.0f}}, {.source = {{0.25f, 0.5f}, {0.25f, 0.5f}}});
    EXPECT_EQ(canvas.Vertices()[0].uv, glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(canvas.Vertices()[2].uv, glm::vec2(0.5f, 1.0f));
}

TEST(Canvas, RotationTurnsClockwiseAroundTheCenter) {
    Canvas canvas = Begun();
    canvas.DrawImage(Texture(1), {{0.0f, 0.0f}, {20.0f, 10.0f}}, {.rotation = glm::half_pi<float>()});
    // The top-left corner of a 20 by 10 rectangle centered on (10, 5) goes to the top right.
    const glm::vec2 topLeft = canvas.Vertices()[0].position;
    EXPECT_NEAR(topLeft.x, 15.0f, 1e-4f);
    EXPECT_NEAR(topLeft.y, -5.0f, 1e-4f);
}

TEST(Canvas, ColorsArePackedRedFirst) {
    EXPECT_EQ(ui::PackColor({1.0f, 0.0f, 0.0f, 1.0f}), 0xFF0000FFU);
    EXPECT_EQ(ui::PackColor({0.0f, 0.0f, 1.0f, 0.0f}), 0x00FF0000U);
    EXPECT_EQ(ui::PackColor({2.0f, -1.0f, 0.5f, 1.0f}), 0xFF8000FFU);
}

TEST(MeasureText, AddsAdvancesAndKerningAtTheSize) {
    const ui::Font font = TestFont();
    EXPECT_EQ(ui::MeasureText(font, "A A", 10.0f), glm::vec2(25.0f, 10.0f));
    EXPECT_EQ(ui::MeasureText(font, "AA", 10.0f), glm::vec2(18.0f, 10.0f));
    EXPECT_EQ(ui::MeasureText(font, "A", 20.0f), glm::vec2(20.0f, 20.0f));
    EXPECT_EQ(ui::MeasureText(font, "", 10.0f), glm::vec2(0.0f, 10.0f));
}

TEST(MeasureText, LinesAddTheirStep) {
    const ui::Font font = TestFont();
    // Each line after the first adds ascent - descent + gap: 12 at size 10.
    EXPECT_EQ(ui::MeasureText(font, "A\nA A\nA", 10.0f), glm::vec2(25.0f, 34.0f));
}

TEST(Canvas, TextPlacesGlyphsFromTheBaseline) {
    const ui::Font font = TestFont();
    Canvas canvas = Begun();
    canvas.DrawText(font, "A A", {100.0f, 50.0f}, {.size = 20.0f});
    // The space has no quad.
    ASSERT_EQ(canvas.Vertices().size(), 8U);
    ASSERT_EQ(canvas.Commands().size(), 1U);
    EXPECT_EQ(canvas.Commands()[0].shading, ui::Shading::Text);
    // Baseline at 50 + 8 * 2; glyphs start 1 * 2 after the pen and 8 * 2 above the baseline.
    EXPECT_EQ(canvas.Vertices()[0].position, glm::vec2(102.0f, 50.0f));
    EXPECT_EQ(canvas.Vertices()[2].position, glm::vec2(118.0f, 70.0f));
    EXPECT_EQ(canvas.Vertices()[4].position, glm::vec2(132.0f, 50.0f));
    EXPECT_EQ(canvas.Vertices()[2].uv, glm::vec2(0.5f, 0.5f));
}

TEST(Canvas, TextAlignsEachLineOnTheAnchor) {
    const ui::Font font = TestFont();
    Canvas canvas = Begun();
    canvas.DrawText(font, "A\nAA", {100.0f, 100.0f},
                    {.size = 10.0f, .horizontal = ui::Align::End, .vertical = ui::Align::Center});
    ASSERT_EQ(canvas.Vertices().size(), 12U);
    // Two lines are 22 high, so the first starts 11 above the anchor. Each ends at the anchor.
    EXPECT_EQ(canvas.Vertices()[0].position, glm::vec2(91.0f, 89.0f));
    EXPECT_EQ(canvas.Vertices()[4].position, glm::vec2(83.0f, 101.0f));
    EXPECT_EQ(canvas.Vertices()[8].position, glm::vec2(91.0f, 101.0f));
}

TEST(Canvas, MissingCharactersDrawTheFallback) {
    const ui::Font font = TestFont();
    Canvas canvas = Begun();
    canvas.DrawText(font, "\xC3\xA9", {0.0f, 0.0f}, {.size = 10.0f});
    EXPECT_EQ(canvas.Vertices().size(), 4U);
    EXPECT_EQ(ui::MeasureText(font, "\xC3\xA9", 10.0f).x, 10.0f);
}

TEST(Canvas, ShadowIsDrawnFirstAtItsOffset) {
    const ui::Font font = TestFont();
    Canvas canvas = Begun();
    canvas.DrawText(font, "A", {0.0f, 0.0f},
                    {.size = 10.0f,
                     .color = glm::vec4(1.0f),
                     .shadowColor = {0.0f, 0.0f, 0.0f, 1.0f},
                     .shadowOffset = {3.0f, 4.0f}});
    ASSERT_EQ(canvas.Vertices().size(), 8U);
    EXPECT_EQ(canvas.Vertices()[0].position, glm::vec2(4.0f, 4.0f));
    EXPECT_EQ(canvas.Vertices()[0].color, 0xFF000000U);
    EXPECT_EQ(canvas.Vertices()[4].position, glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(canvas.Vertices()[4].color, 0xFFFFFFFFU);
}
