// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <cmath>

#include "CameraEffects.hpp"

namespace {

using daidalos::renderer::CircleOfConfusionScale;

// A 50 mm lens on a full-frame sensor: about 27 degrees of vertical field of view.
const float kFiftyMillimeters = 2.0f * std::atan(0.012f / 0.05f);

TEST(CameraEffectsTest, NoApertureMeansNoBlur) {
    EXPECT_EQ(CircleOfConfusionScale(0.0f, 3.0f, kFiftyMillimeters, 1080), 0.0f);
    EXPECT_EQ(CircleOfConfusionScale(2.8f, 0.0f, kFiftyMillimeters, 1080), 0.0f);
}

TEST(CameraEffectsTest, BlurFollowsTheThinLens) {
    // f = 50 mm at f/2 focused at 2 m: f^2 / (N (s - f)) = 0.0025 / (2 * 1.95) m on the sensor, over 24 mm of
    // height, times 1000 pixels.
    const float scale = CircleOfConfusionScale(2.0f, 2.0f, kFiftyMillimeters, 1000);
    EXPECT_NEAR(scale, 0.0025f / (2.0f * 1.95f) / 0.024f * 1000.0f, 0.05f);
}

TEST(CameraEffectsTest, WiderAperturesAndLongerLensesBlurMore) {
    const float f8 = CircleOfConfusionScale(8.0f, 3.0f, kFiftyMillimeters, 1080);
    const float f2 = CircleOfConfusionScale(2.0f, 3.0f, kFiftyMillimeters, 1080);
    EXPECT_NEAR(f2, 4.0f * f8, 0.01f * f2);
    const float wide = CircleOfConfusionScale(2.0f, 3.0f, 1.2f, 1080);
    EXPECT_LT(wide, f2);
}

} // namespace
