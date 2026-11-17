// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <cmath>

#include "Flashlight.hpp"

using sandbox::FlashlightState;

namespace {

TEST(Flashlight, TheBeamTrailsATurnAndCatchesUp) {
    FlashlightState state;
    sandbox::SwayFlashlight(state, 0.1f, 0.0f, 0.0f, 1.0f / 60.0f);
    EXPECT_LT(state.lag.x, -0.05f) << "Turned right, the hand is still left of the aim";
    for (int frame = 0; frame < 60; ++frame) {
        sandbox::SwayFlashlight(state, 0.0f, 0.0f, 0.0f, 1.0f / 60.0f);
    }
    EXPECT_NEAR(state.lag.x, 0.0f, 0.005f) << "A second later, it has caught up";
    EXPECT_GE(state.lag.x, -0.005f) << "Without swinging far past";
}

TEST(Flashlight, AFastSpinIsHeldWithinReach) {
    FlashlightState state;
    sandbox::SwayFlashlight(state, 3.0f, -3.0f, 0.0f, 1.0f / 60.0f);
    EXPECT_LE(std::abs(state.lag.x), 0.14f);
    EXPECT_LE(std::abs(state.lag.y), 0.14f);
}

TEST(Flashlight, WalkingSwaysTheBeamAndStandingDoesNot) {
    FlashlightState standing;
    FlashlightState walking;
    float moved = 0.0f;
    for (int frame = 0; frame < 120; ++frame) {
        sandbox::SwayFlashlight(standing, 0.0f, 0.0f, 0.0f, 1.0f / 60.0f);
        sandbox::SwayFlashlight(walking, 0.0f, 0.0f, 4.0f, 1.0f / 60.0f);
        moved = std::max(moved, std::abs(sandbox::FlashlightAim(walking).y));
    }
    EXPECT_EQ(sandbox::FlashlightAim(standing).y, 0.0f);
    EXPECT_GT(moved, 0.005f);
}

TEST(Flashlight, TheBatteryRunsDownDimsAndDies) {
    FlashlightState state;
    EXPECT_FLOAT_EQ(sandbox::FlashlightBrightness(state, 1.0), 1.0f);
    sandbox::DrainFlashlight(state, 90.0f, 100.0f);
    EXPECT_NEAR(state.battery, 0.1f, 1e-5f);
    float total = 0.0f;
    for (int step = 0; step < 100; ++step) {
        total += sandbox::FlashlightBrightness(state, step * 0.05) / 100.0f;
    }
    EXPECT_LT(total, 0.9f) << "Low, it is dimmer";
    EXPECT_GT(total, 0.2f);
    sandbox::DrainFlashlight(state, 50.0f, 100.0f);
    EXPECT_FLOAT_EQ(state.battery, 0.0f);
    EXPECT_FLOAT_EQ(sandbox::FlashlightBrightness(state, 1.0), 0.0f);

    FlashlightState endless;
    sandbox::DrainFlashlight(endless, 1000.0f, 0.0f);
    EXPECT_FLOAT_EQ(endless.battery, 1.0f) << "A lifetime of 0 never drains";
}

TEST(Flashlight, AFaultMakesItFailUntilItEnds) {
    FlashlightState state;
    state.fault = 2.0f;
    float lowest = 1.0f;
    for (int step = 0; step < 100; ++step) {
        lowest = std::min(lowest, sandbox::FlashlightBrightness(state, step * 0.02));
    }
    EXPECT_LT(lowest, 0.1f);
    sandbox::DrainFlashlight(state, 3.0f, 0.0f);
    EXPECT_FLOAT_EQ(state.fault, 0.0f);
    EXPECT_FLOAT_EQ(sandbox::FlashlightBrightness(state, 0.5), 1.0f);
}

} // namespace
