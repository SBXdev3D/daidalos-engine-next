// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <glm/vec2.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "TemporalResolvePass.hpp"

using daidalos::renderer::Jitter;
using daidalos::renderer::kJitterPositions;

TEST(Jitter, FollowsTheHaltonSequenceFromItsSecondPoint) {
    // Halton 1 in bases 2 and 3 is (1/2, 1/3); 2 is (1/4, 2/3); both centered on the pixel.
    EXPECT_FLOAT_EQ(Jitter(0).x, 0.0f);
    EXPECT_FLOAT_EQ(Jitter(0).y, 1.0f / 3.0f - 0.5f);
    EXPECT_FLOAT_EQ(Jitter(1).x, -0.25f);
    EXPECT_FLOAT_EQ(Jitter(1).y, 2.0f / 3.0f - 0.5f);
}

TEST(Jitter, StaysInsideThePixelAndRepeats) {
    std::vector<glm::vec2> seen;
    glm::vec2 sum(0.0f);
    for (std::uint64_t frame = 0; frame < kJitterPositions; ++frame) {
        const glm::vec2 jitter = Jitter(frame);
        EXPECT_GE(jitter.x, -0.5f);
        EXPECT_LT(jitter.x, 0.5f);
        EXPECT_GE(jitter.y, -0.5f);
        EXPECT_LT(jitter.y, 0.5f);
        for (const glm::vec2& earlier : seen) {
            EXPECT_NE(jitter, earlier) << "frame " << frame;
        }
        seen.push_back(jitter);
        sum += jitter;
        EXPECT_EQ(Jitter(frame + kJitterPositions), jitter);
    }
    // Spread around the center, not to one side.
    EXPECT_LT(std::abs(sum.x / kJitterPositions), 0.1f);
    EXPECT_LT(std::abs(sum.y / kJitterPositions), 0.1f);
}
