// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/DisplayTransform.hpp>

#include <gtest/gtest.h>

#include <algorithm>

using namespace daidalos;

namespace {

float Largest(const glm::vec3& color) {
    return std::max({color.r, color.g, color.b});
}

} // namespace

TEST(AgX, BlackStaysBlack) {
    const glm::vec3 black = renderer::AgX(glm::vec3(0.0f));
    EXPECT_NEAR(Largest(black), 0.0f, 0.001f);
}

TEST(AgX, GreysStayGreyAndGetBrighter) {
    float previous = -1.0f;
    for (const float grey : {0.001f, 0.01f, 0.05f, 0.18f, 0.5f, 1.0f, 4.0f, 16.0f}) {
        const glm::vec3 shown = renderer::AgX(glm::vec3(grey));
        EXPECT_NEAR(shown.r, shown.g, 0.01f) << grey;
        EXPECT_NEAR(shown.g, shown.b, 0.01f) << grey;
        EXPECT_GT(shown.g, previous) << grey;
        previous = shown.g;
    }
}

TEST(AgX, MiddleGreyStaysNearMiddleGrey) {
    const glm::vec3 shown = renderer::AgX(glm::vec3(0.18f));
    EXPECT_NEAR(shown.g, 0.18f, 0.05f);
}

TEST(AgX, BrightLightGoesTowardsWhiteInsteadOfClipping) {
    // A bright saturated red: PBR Neutral keeps it red, AgX lets its other channels rise.
    const glm::vec3 lamp = renderer::AgX(glm::vec3(40.0f, 2.0f, 0.5f));
    EXPECT_LE(Largest(lamp), 1.0f);
    EXPECT_GT(lamp.g, 0.3f);
    EXPECT_GT(lamp.r, lamp.g);
}

TEST(PbrNeutral, KeepsDarkColorsAsTheyAre) {
    const glm::vec3 color{0.3f, 0.2f, 0.1f};
    const glm::vec3 shown = renderer::PbrNeutral(color);
    EXPECT_NEAR(shown.r, 0.3f - 0.04f, 0.001f);
    EXPECT_NEAR(shown.b, 0.1f - 0.04f, 0.001f);
}

TEST(ExpandHighlights, OneBecomesTheHeadroomAndDarkBarelyMoves) {
    EXPECT_NEAR(Largest(renderer::ExpandHighlights(glm::vec3(1.0f), 4.0f)), 4.0f, 0.001f);
    const glm::vec3 grey = renderer::ExpandHighlights(glm::vec3(0.18f), 4.0f);
    EXPECT_NEAR(grey.g, 0.18f, 0.18f * 0.2f);
    const glm::vec3 dark = renderer::ExpandHighlights(glm::vec3(0.01f), 4.0f);
    EXPECT_NEAR(dark.g, 0.01f, 0.0002f);
}

TEST(ExpandHighlights, KeepsTheHue) {
    const glm::vec3 color{0.9f, 0.45f, 0.1f};
    const glm::vec3 expanded = renderer::ExpandHighlights(color, 6.0f);
    EXPECT_NEAR(expanded.g / expanded.r, 0.5f, 0.0001f);
    EXPECT_NEAR(expanded.b / expanded.r, color.b / color.r, 0.0001f);
}

TEST(ExpandHighlights, ChangesNothingInSdr) {
    const glm::vec3 color{0.7f, 0.2f, 0.4f};
    const glm::vec3 same = renderer::ExpandHighlights(color, 1.0f);
    EXPECT_FLOAT_EQ(same.r, color.r);
    EXPECT_FLOAT_EQ(same.b, color.b);
}

TEST(DisplayColor, IsTheToneMapThenTheExpansion) {
    const glm::vec3 scene{2.0f, 0.5f, 0.1f};
    const glm::vec3 shown = renderer::DisplayColor(scene, renderer::ToneMapper::AgX, 3.0f);
    const glm::vec3 expected = renderer::ExpandHighlights(renderer::AgX(scene), 3.0f);
    EXPECT_FLOAT_EQ(shown.r, expected.r);
    EXPECT_FLOAT_EQ(shown.g, expected.g);
}

TEST(Rec709ToRec2020, KeepsWhite) {
    const glm::vec3 white = renderer::Rec709ToRec2020(glm::vec3(1.0f));
    EXPECT_NEAR(white.r, 1.0f, 0.001f);
    EXPECT_NEAR(white.g, 1.0f, 0.001f);
    EXPECT_NEAR(white.b, 1.0f, 0.001f);
}

TEST(PqEncode, MatchesKnownValues) {
    // SMPTE ST 2084: 0 nits is 0, 100 nits about 0.508, 1000 nits about 0.752, 10,000 nits 1.
    EXPECT_NEAR(renderer::PqEncode(0.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(renderer::PqEncode(100.0f), 0.5081f, 0.001f);
    EXPECT_NEAR(renderer::PqEncode(1000.0f), 0.7518f, 0.001f);
    EXPECT_NEAR(renderer::PqEncode(10000.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(renderer::PqEncode(20000.0f), 1.0f, 0.0001f);
}
