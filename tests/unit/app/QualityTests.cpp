// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Quality.hpp>

#include <gtest/gtest.h>

using namespace daidalos::app;

TEST(Quality, HighIsTheEnginesDefaults) {
    EXPECT_EQ(SettingsOf(Quality::High), QualitySettings{});
}

TEST(Quality, EachPresetCostsMoreThanTheOneBelow) {
    // Every setting with a cost goes the same way from Low to Ultra, or stays.
    for (std::size_t index = 1; index < kQualities.size(); ++index) {
        const QualitySettings lower = SettingsOf(kQualities[index - 1]);
        const QualitySettings higher = SettingsOf(kQualities[index]);
        EXPECT_LE(lower.renderScale, higher.renderScale) << NameOf(kQualities[index]);
        EXPECT_LE(lower.minRenderScale, higher.minRenderScale) << NameOf(kQualities[index]);
        EXPECT_LE(lower.giBounces, higher.giBounces) << NameOf(kQualities[index]);
        EXPECT_LE(lower.giRays, higher.giRays) << NameOf(kQualities[index]);
        EXPECT_LE(lower.reflectionRoughness, higher.reflectionRoughness) << NameOf(kQualities[index]);
        EXPECT_LE(lower.restirSpatial, higher.restirSpatial) << NameOf(kQualities[index]);
        EXPECT_LE(lower.fogSlices, higher.fogSlices) << NameOf(kQualities[index]);
        EXPECT_GE(lower.fogTile, higher.fogTile) << NameOf(kQualities[index]);
        EXPECT_NE(lower, higher);
    }
}

TEST(Quality, NamesGoBothWays) {
    for (const Quality quality : kQualities) {
        EXPECT_EQ(QualityNamed(NameOf(quality)), quality);
    }
    EXPECT_FALSE(QualityNamed("auto").has_value());
    EXPECT_FALSE(QualityNamed("High").has_value()) << "Names are lowercase, as cvars hold them";
}

TEST(Quality, TheFirstLaunchChoosesWhatHoldsTheFrameRate) {
    // An RTX 3090 at 1080p: High at full scale takes 7.7 ms, and Ultra fits 60 fps.
    EXPECT_EQ(ChooseQuality(7.7, 60.0), Quality::Ultra);
    // About 2.2 times slower, an RTX 3060: High's 17 ms do not fit, Medium does.
    EXPECT_EQ(ChooseQuality(17.0, 60.0), Quality::Medium);
    // At 120 fps, the same GPU steps down to Medium: High's 7.7 ms leave no margin in 8.3.
    EXPECT_EQ(ChooseQuality(7.7, 120.0), Quality::Medium);
    EXPECT_EQ(ChooseQuality(11.0, 60.0), Quality::High);
    EXPECT_EQ(ChooseQuality(30.0, 60.0), Quality::Low);
    EXPECT_EQ(ChooseQuality(100.0, 60.0), Quality::Low) << "Low is the floor";
}
