// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "Exposure.hpp"

using namespace daidalos;

namespace {

using Histogram = std::array<std::uint32_t, renderer::kHistogramBins>;

/// The bin that holds a luminance in nits, as the shader picks it.
std::uint32_t BinOf(float nits) {
    const float position =
        (std::log2(nits) - renderer::kHistogramLowest) / (renderer::kHistogramHighest - renderer::kHistogramLowest);
    return 1 + std::min(static_cast<std::uint32_t>(position * static_cast<float>(renderer::kHistogramBins - 1)),
                        renderer::kHistogramBins - 2);
}

// One bin spans 26 / 127 of a stop.
constexpr float kBinStops = 0.21f;

} // namespace

TEST(MeterEv100, AnEmptyHistogramMetersNothing) {
    const Histogram empty{};
    EXPECT_FALSE(renderer::MeterEv100(empty).has_value());
}

TEST(MeterEv100, OneNitIsEv100Three) {
    // The reflected-light meter: EV100 = log2(L * 100 / 12.5).
    Histogram histogram{};
    histogram[BinOf(1.0f)] = 1000;
    const std::optional<float> ev100 = renderer::MeterEv100(histogram);
    ASSERT_TRUE(ev100.has_value());
    EXPECT_NEAR(*ev100, 3.0f, kBinStops);
}

TEST(MeterEv100, EachStopOfLightIsOneEv) {
    for (const float nits : {0.01f, 0.25f, 4.0f, 100.0f, 5000.0f}) {
        Histogram histogram{};
        histogram[BinOf(nits)] = 50;
        EXPECT_NEAR(*renderer::MeterEv100(histogram), std::log2(nits) + 3.0f, kBinStops) << nits;
    }
}

TEST(MeterEv100, IgnoresTheDarkestAndTheBrightestPixels) {
    // A dim room with black corners (40% of the weight) and a lamp (5%): the room decides.
    Histogram histogram{};
    histogram[0] = 400;
    histogram[BinOf(2.0f)] = 550;
    histogram[BinOf(20000.0f)] = 50;
    EXPECT_NEAR(*renderer::MeterEv100(histogram), 4.0f, kBinStops);
}

TEST(MeterEv100, EqualFractionsTakeTheBinWhereTheyFall) {
    Histogram histogram{};
    histogram[BinOf(0.5f)] = 10;
    histogram[BinOf(8.0f)] = 10;
    EXPECT_NEAR(*renderer::MeterEv100(histogram, 0.75f, 0.75f), 6.0f, kBinStops);
}

TEST(EyeAdaptation, StartsWhereItIsReset) {
    renderer::EyeAdaptation eye;
    EXPECT_FALSE(eye.Started());
    eye.Reset(3.0f);
    EXPECT_TRUE(eye.Started());
    EXPECT_FLOAT_EQ(eye.Ev100(), 3.0f);
}

TEST(EyeAdaptation, ClosesFastAndOpensSlowly) {
    renderer::EyeAdaptation brighter;
    brighter.Reset(3.0f);
    brighter.Update(5.0f, 0.5f, 0.0f, 10.0f);
    renderer::EyeAdaptation darker;
    darker.Reset(3.0f);
    darker.Update(1.0f, 0.5f, 0.0f, 10.0f);
    // A time constant towards the light: 63% of the way.
    EXPECT_NEAR(brighter.Ev100(), 3.0f + (2.0f * (1.0f - std::exp(-1.0f))), 0.001f);
    EXPECT_LT(3.0f - darker.Ev100(), (brighter.Ev100() - 3.0f) / 3.0f);
}

TEST(EyeAdaptation, GetsThereInTheEnd) {
    renderer::EyeAdaptation eye;
    eye.Reset(3.0f);
    for (int frame = 0; frame < 60 * 30; ++frame) {
        eye.Update(0.5f, 1.0f / 60.0f, -5.0f, 10.0f);
    }
    EXPECT_NEAR(eye.Ev100(), 0.5f, 0.01f);
}

TEST(EyeAdaptation, StaysInTheLevelsRange) {
    // A dark corridor meters far below the level's darkest: the eye stops there, and the corridor stays dark.
    renderer::EyeAdaptation eye;
    eye.Reset(3.0f);
    for (int frame = 0; frame < 60 * 30; ++frame) {
        eye.Update(-4.0f, 1.0f / 60.0f, 2.0f, 5.0f);
    }
    EXPECT_NEAR(eye.Ev100(), 2.0f, 0.001f);
    eye.Update(12.0f, 30.0f, 2.0f, 5.0f);
    EXPECT_FLOAT_EQ(eye.Ev100(), 5.0f);
}

TEST(EyeAdaptation, StaysWithoutAMetering) {
    renderer::EyeAdaptation eye;
    eye.Reset(3.0f);
    eye.Update(std::nullopt, 1.0f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(eye.Ev100(), 3.0f);
    // A narrower range still holds it.
    eye.Update(std::nullopt, 1.0f, 4.0f, 6.0f);
    EXPECT_FLOAT_EQ(eye.Ev100(), 4.0f);
}

TEST(EyeAdaptation, StandsStillWhenTimeDoes) {
    renderer::EyeAdaptation eye;
    eye.Reset(3.0f);
    eye.Update(8.0f, 0.0f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(eye.Ev100(), 3.0f);
}
