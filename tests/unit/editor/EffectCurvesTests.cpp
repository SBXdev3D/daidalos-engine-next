// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include "EffectPanel.hpp"

using namespace editor;

TEST(EffectCurvesTest, CurvesBlendAsTheGpuDoes) {
    const std::vector<std::pair<float, float>> keys{{0.25f, 1.0f}, {0.75f, 3.0f}};
    EXPECT_FLOAT_EQ(CurveAt(keys, 0.0f), 1.0f); // Held before the first key.
    EXPECT_FLOAT_EQ(CurveAt(keys, 0.5f), 2.0f);
    EXPECT_FLOAT_EQ(CurveAt(keys, 1.0f), 3.0f); // And after the last.
    EXPECT_FLOAT_EQ(CurveAt({}, 0.5f), 0.0f);
}

TEST(EffectCurvesTest, KeysAreSortedWithinLife) {
    const auto sorted = SortedKeys({{0.8f, 1.0f}, {-0.5f, 2.0f}, {1.5f, 3.0f}, {0.2f, 4.0f}});
    ASSERT_EQ(sorted.size(), 4U);
    EXPECT_FLOAT_EQ(sorted[0].first, 0.0f);
    EXPECT_FLOAT_EQ(sorted[0].second, 2.0f);
    EXPECT_FLOAT_EQ(sorted[1].first, 0.2f);
    EXPECT_FLOAT_EQ(sorted[3].first, 1.0f);
}
