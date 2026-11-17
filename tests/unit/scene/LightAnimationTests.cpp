// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/LightAnimation.hpp>
#include <daidalos/scene/Systems.hpp>

#include <gtest/gtest.h>

#include <algorithm>

using namespace daidalos;
using scene::LightAnimation;
using scene::LightPattern;

namespace {

struct Range {
    float low = 1.0f;
    float high = 0.0f;
    float mean = 0.0f;
};

// The factors of ten seconds of a pattern, 100 a second.
Range Sample(const LightAnimation& animation, std::uint32_t seed = 7) {
    Range range;
    constexpr int kSamples = 1000;
    for (int step = 0; step < kSamples; ++step) {
        const float factor = scene::LightFactor(animation, step * 0.01, seed);
        range.low = std::min(range.low, factor);
        range.high = std::max(range.high, factor);
        range.mean += factor / kSamples;
    }
    return range;
}

TEST(LightAnimation, SteadyIsAlwaysFull) {
    const Range range = Sample({});
    EXPECT_FLOAT_EQ(range.low, 1.0f);
    EXPECT_FLOAT_EQ(range.high, 1.0f);
}

TEST(LightAnimation, EachPatternKeepsToItsCharacter) {
    const Range flicker = Sample({.pattern = LightPattern::Flicker});
    EXPECT_GT(flicker.low, 0.6f) << "A flame never goes out";
    EXPECT_LT(flicker.high - flicker.low, 0.4f);
    EXPECT_GT(flicker.high - flicker.low, 0.05f) << "But it moves";

    const Range buzz = Sample({.pattern = LightPattern::Buzz});
    EXPECT_GT(buzz.mean, 0.85f) << "A tired tube is mostly on";
    EXPECT_LT(buzz.low, 0.3f) << "With dropouts";

    const Range failing = Sample({.pattern = LightPattern::Failing, .rate = 1.0f}, 3);
    EXPECT_LT(failing.mean, 0.7f) << "A dying light is often dark";
    EXPECT_LT(failing.low, 0.06f);
    EXPECT_GT(failing.high, 0.8f);

    const Range strobe = Sample({.pattern = LightPattern::Strobe, .rate = 2.0f});
    EXPECT_FLOAT_EQ(strobe.low, 0.0f);
    EXPECT_FLOAT_EQ(strobe.high, 1.0f);
    EXPECT_NEAR(strobe.mean, 0.12f, 0.05f);

    const Range pulse = Sample({.pattern = LightPattern::Pulse});
    EXPECT_NEAR(pulse.mean, 0.5f, 0.05f);
}

TEST(LightAnimation, DepthScalesTheChange) {
    const Range shallow = Sample({.pattern = LightPattern::Strobe, .depth = 0.25f});
    EXPECT_FLOAT_EQ(shallow.low, 0.75f);
    const Range none = Sample({.pattern = LightPattern::Failing, .depth = 0.0f});
    EXPECT_FLOAT_EQ(none.low, 1.0f);
}

TEST(LightAnimation, SeedsSetLightsApartAndTheSameSeedRepeats) {
    const LightAnimation candle{.pattern = LightPattern::Flicker};
    EXPECT_EQ(scene::LightFactor(candle, 1.234, 5), scene::LightFactor(candle, 1.234, 5));
    bool differ = false;
    for (int step = 0; step < 50; ++step) {
        differ |= scene::LightFactor(candle, step * 0.1, 5) != scene::LightFactor(candle, step * 0.1, 6);
    }
    EXPECT_TRUE(differ);
}

TEST(LightAnimation, NamesGoBothWays) {
    for (const LightPattern pattern : {LightPattern::Steady, LightPattern::Flicker, LightPattern::Buzz,
                                       LightPattern::Failing, LightPattern::Strobe, LightPattern::Pulse}) {
        LightPattern read = LightPattern::Steady;
        ASSERT_TRUE(scene::LightPatternOf(scene::LightPatternName(pattern), read));
        EXPECT_EQ(read, pattern);
    }
    LightPattern read = LightPattern::Steady;
    EXPECT_FALSE(scene::LightPatternOf("disco", read));
}

TEST(PowerCircuits, ACutGoesDarkAndAReturnBlinksOn) {
    scene::PowerCircuits circuits;
    EXPECT_FLOAT_EQ(circuits.Factor("hall", 10.0), 1.0f) << "A circuit no one switched is on";
    circuits.Set("hall", false, 10.0);
    EXPECT_FALSE(circuits.IsOn("hall"));
    EXPECT_GT(circuits.Factor("hall", 10.05), 0.0f) << "It sags first";
    EXPECT_FLOAT_EQ(circuits.Factor("hall", 10.01 + scene::PowerCircuits::kCutSeconds), 0.0f);
    EXPECT_FLOAT_EQ(circuits.Factor("hall", 20.0), 0.0f);
    circuits.Set("hall", true, 30.0);
    EXPECT_FLOAT_EQ(circuits.Factor("hall", 30.05), 0.0f) << "Starting, a tube is dark";
    EXPECT_FLOAT_EQ(circuits.Factor("hall", 30.0 + scene::PowerCircuits::kStartSeconds), 1.0f);
    EXPECT_FLOAT_EQ(circuits.Factor("cellar", 30.5), 1.0f) << "Other circuits keep their power";
}

TEST(AnimateLights, DrivesTheLightAndItsLampsGlow) {
    ecs::World world;
    const ecs::Entity lamp = world.Create();
    world.Add<scene::ModelInstance>(lamp);
    const ecs::Entity bulb = world.Create();
    ASSERT_TRUE(world.SetParent(bulb, lamp).has_value());
    world.Add<scene::Light>(bulb, scene::Light{.animation = {.pattern = LightPattern::Strobe, .circuit = "hall"}});
    const ecs::Entity steady = world.Create();
    world.Add<scene::Light>(steady);

    scene::PowerCircuits circuits;
    scene::AnimateLights(world, 0.0, circuits);
    const float factor = scene::LightFactor({.pattern = LightPattern::Strobe}, 0.0, bulb.Index() * 2654435761U);
    ASSERT_NE(world.TryGet<scene::LightState>(bulb), nullptr);
    EXPECT_FLOAT_EQ(world.Get<scene::LightState>(bulb).factor, factor);
    ASSERT_NE(world.TryGet<scene::EmissiveScale>(lamp), nullptr) << "The lamp it hangs from glows with it";
    EXPECT_FLOAT_EQ(world.Get<scene::EmissiveScale>(lamp).value, factor);
    EXPECT_EQ(world.TryGet<scene::LightState>(steady), nullptr) << "A steady light costs nothing";

    circuits.Set("hall", false, 0.0);
    scene::AnimateLights(world, 5.0, circuits);
    EXPECT_FLOAT_EQ(world.Get<scene::LightState>(bulb).factor, 0.0f);
    EXPECT_FLOAT_EQ(world.Get<scene::EmissiveScale>(lamp).value, 0.0f);
}

} // namespace
