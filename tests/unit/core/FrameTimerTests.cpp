// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/FrameTimer.hpp>

#include <gtest/gtest.h>

#include <cstdint>

using namespace daidalos;
using core::FrameTime;
using core::FrameTimer;
using core::FrameTimerConfig;
using core::Seconds;

namespace {

// Powers of two keep every sum exact, so the expectations below can use EXPECT_EQ.
FrameTimerConfig QuarterSecondSteps() {
    FrameTimerConfig config;
    config.fixedStep = Seconds{0.25};
    config.maxDelta = Seconds{1.0};
    return config;
}

} // namespace

TEST(FrameTimer, FrameIndexStartsAtZeroAndIncrements) {
    FrameTimer timer;

    EXPECT_EQ(timer.Advance(Seconds{0.01}).frameIndex, 0u);
    EXPECT_EQ(timer.Advance(Seconds{0.01}).frameIndex, 1u);
    EXPECT_EQ(timer.Advance(Seconds{0.01}).frameIndex, 2u);
}

TEST(FrameTimer, DeltaIsTheElapsedTimeWhenBelowMaximum) {
    FrameTimer timer(QuarterSecondSteps());

    const FrameTime time = timer.Advance(Seconds{0.5});

    EXPECT_EQ(time.delta, Seconds{0.5});
    EXPECT_EQ(time.unclampedDelta, Seconds{0.5});
}

TEST(FrameTimer, LongFrameIsClampedToMaximumDelta) {
    FrameTimer timer(QuarterSecondSteps());

    const FrameTime time = timer.Advance(Seconds{10.0});

    EXPECT_EQ(time.delta, Seconds{1.0});
    EXPECT_EQ(time.unclampedDelta, Seconds{10.0});
    EXPECT_EQ(time.fixedSteps, 4u) << "Clamping also limits the number of fixed steps";
}

TEST(FrameTimer, NegativeElapsedTimeCountsAsZero) {
    FrameTimer timer(QuarterSecondSteps());

    const FrameTime time = timer.Advance(Seconds{-1.0});

    EXPECT_EQ(time.delta, Seconds::zero());
    EXPECT_EQ(time.fixedSteps, 0u);
}

TEST(FrameTimer, TotalIsTheSumOfClampedDeltas) {
    FrameTimer timer(QuarterSecondSteps());

    timer.Advance(Seconds{0.5});
    timer.Advance(Seconds{5.0});
    const FrameTime time = timer.Advance(Seconds{0.25});

    EXPECT_EQ(time.total, Seconds{1.75});
}

TEST(FrameTimer, AccumulatorYieldsWholeStepsAndRemainingFraction) {
    FrameTimer timer(QuarterSecondSteps());

    const FrameTime first = timer.Advance(Seconds{0.625}); // 2 steps, 0.125 left
    EXPECT_EQ(first.fixedSteps, 2u);
    EXPECT_DOUBLE_EQ(first.alpha, 0.5);

    const FrameTime second = timer.Advance(Seconds{0.125}); // 0.25 accumulated: 1 step, nothing left
    EXPECT_EQ(second.fixedSteps, 1u);
    EXPECT_DOUBLE_EQ(second.alpha, 0.0);
}

TEST(FrameTimer, ShortFramesAccumulateUntilAStepIsReached) {
    FrameTimer timer(QuarterSecondSteps());

    EXPECT_EQ(timer.Advance(Seconds{0.125}).fixedSteps, 0u);
    EXPECT_EQ(timer.Advance(Seconds{0.0625}).fixedSteps, 0u);
    const FrameTime time = timer.Advance(Seconds{0.0625});

    EXPECT_EQ(time.fixedSteps, 1u);
    EXPECT_DOUBLE_EQ(time.alpha, 0.0);
}

TEST(FrameTimer, AlphaStaysInUnitRange) {
    FrameTimer timer;

    for (int i = 0; i < 1000; ++i) {
        const FrameTime time = timer.Advance(Seconds{0.001 * (i % 37)});
        ASSERT_GE(time.alpha, 0.0);
        ASSERT_LT(time.alpha, 1.0);
    }
}

TEST(FrameTimer, SixtyHertzFramesRunOneStepEach) {
    FrameTimer timer; // default fixed step: 1/60 s

    std::uint64_t steps = 0;
    for (int i = 0; i < 600; ++i) {
        steps += timer.Advance(Seconds{1.0 / 60.0}).fixedSteps;
    }

    EXPECT_EQ(steps, 600u);
}

TEST(FrameTimer, StepCountMatchesElapsedTimeOverManyUnevenFrames) {
    FrameTimer timer(QuarterSecondSteps());

    std::uint64_t steps = 0;
    for (int i = 0; i < 64; ++i) {
        steps += timer.Advance(Seconds{(i % 2 == 0) ? 0.125 : 0.375}).fixedSteps;
    }

    // 32 * (0.125 + 0.375) = 16 s, that is 64 steps of 0.25 s.
    EXPECT_EQ(steps, 64u);
}
