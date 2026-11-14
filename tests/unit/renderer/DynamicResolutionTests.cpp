// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

#include "DynamicResolution.hpp"

using daidalos::renderer::DynamicResolution;
using daidalos::renderer::DynamicResolutionSettings;
using daidalos::renderer::RenderExtent;

namespace {

constexpr DynamicResolutionSettings kSettings{.enabled = true, .maxScale = 1.0f, .minScale = 0.5f, .budget = 10.0};
constexpr float kFrame = 1.0f / 60.0f;

} // namespace

TEST(DynamicResolution, StartsAtItsCeilingAndStaysUnderBudget) {
    DynamicResolution resolution(2);
    for (int frame = 0; frame < 100; ++frame) {
        EXPECT_FLOAT_EQ(resolution.Update(9.0, kFrame, kSettings), 1.0f);
    }
}

TEST(DynamicResolution, GoesDownAtOnceToWhatTheBudgetAllows) {
    DynamicResolution resolution(2);
    // Twice the budget: half the pixels, a little under.
    const float scale = resolution.Update(20.0, kFrame, kSettings);
    EXPECT_NEAR(scale, std::sqrt(0.5f * DynamicResolution::kMargin), 1e-5f);
}

TEST(DynamicResolution, PassesOverTheFramesTimedBeforeAChange) {
    DynamicResolution resolution(2);
    const float scale = resolution.Update(20.0, kFrame, kSettings);
    // These two were drawn at the old scale.
    EXPECT_FLOAT_EQ(resolution.Update(20.0, kFrame, kSettings), scale);
    EXPECT_FLOAT_EQ(resolution.Update(20.0, kFrame, kSettings), scale);
    EXPECT_LT(resolution.Update(20.0, kFrame, kSettings), scale);
}

TEST(DynamicResolution, GoesUpByAStepAfterHalfASecondWellUnderBudget) {
    DynamicResolution resolution(0);
    const float low = resolution.Update(40.0, kFrame, kSettings);
    ASSERT_FLOAT_EQ(low, 0.5f);
    float scale = low;
    int frames = 0;
    while (scale == low && frames < 1000) {
        scale = resolution.Update(5.0, kFrame, kSettings);
        ++frames;
    }
    EXPECT_NEAR(scale, low + DynamicResolution::kStep, 1e-6f);
    EXPECT_NEAR(static_cast<float>(frames) * kFrame, DynamicResolution::kRaiseSeconds, kFrame * 1.5f);
}

TEST(DynamicResolution, HoldsNearTheBudget) {
    DynamicResolution resolution(0);
    const float scale = resolution.Update(12.0, kFrame, kSettings);
    // Between 85% and 100% of the budget: neither down nor up.
    for (int frame = 0; frame < 600; ++frame) {
        EXPECT_FLOAT_EQ(resolution.Update(9.0, kFrame, kSettings), scale);
    }
}

TEST(DynamicResolution, AFrameOverBudgetRestartsTheWaitToGoUp) {
    DynamicResolution resolution(0);
    const float low = resolution.Update(40.0, kFrame, kSettings);
    for (int frame = 0; frame < 20; ++frame) {
        resolution.Update(5.0, kFrame, kSettings);
    }
    resolution.Update(9.5, kFrame, kSettings); // Near the budget: the wait starts over.
    for (int frame = 0; frame < 20; ++frame) {
        EXPECT_FLOAT_EQ(resolution.Update(5.0, kFrame, kSettings), low);
    }
}

TEST(DynamicResolution, StaysWithinItsBounds) {
    DynamicResolution resolution(0);
    EXPECT_FLOAT_EQ(resolution.Update(1000.0, kFrame, kSettings), 0.5f);
    DynamicResolutionSettings capped = kSettings;
    capped.maxScale = 0.67f;
    for (int frame = 0; frame < 2000; ++frame) {
        resolution.Update(1.0, kFrame, capped);
    }
    EXPECT_FLOAT_EQ(resolution.Scale(), 0.67f);
}

TEST(DynamicResolution, OffKeepsTheCeiling) {
    DynamicResolution resolution(0);
    DynamicResolutionSettings off = kSettings;
    off.enabled = false;
    off.maxScale = 0.58f;
    EXPECT_FLOAT_EQ(resolution.Update(1000.0, kFrame, off), 0.58f);
    EXPECT_FLOAT_EQ(resolution.Update(std::nullopt, kFrame, off), 0.58f);
}

TEST(DynamicResolution, WithoutTimingsStaysWhereItIs) {
    DynamicResolution resolution(0);
    const float scale = resolution.Update(20.0, kFrame, kSettings);
    for (int frame = 0; frame < 100; ++frame) {
        EXPECT_FLOAT_EQ(resolution.Update(std::nullopt, kFrame, kSettings), scale);
    }
}

TEST(RenderExtent, RoundsToWholePixelsAndKeepsOne) {
    EXPECT_EQ(RenderExtent({1920, 1080}, 1.0f), (daidalos::rhi::Extent{1920, 1080}));
    EXPECT_EQ(RenderExtent({1920, 1080}, 0.67f), (daidalos::rhi::Extent{1286, 724}));
    EXPECT_EQ(RenderExtent({2560, 1440}, 0.5f), (daidalos::rhi::Extent{1280, 720}));
    EXPECT_EQ(RenderExtent({1, 1}, 0.25f), (daidalos::rhi::Extent{1, 1}));
}
