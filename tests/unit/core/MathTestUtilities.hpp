// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <gtest/gtest.h>

#include <format>
#include <string>

namespace daidalos::test {

inline constexpr float kTolerance = 1e-5f;

inline std::string ToString(const glm::vec3& value) {
    return std::format("({}, {}, {})", value.x, value.y, value.z);
}

// Compares two vectors with a tolerance and prints both on failure.
inline testing::AssertionResult NearlyEqual(const glm::vec3& actual, const glm::vec3& expected,
                                            float tolerance = kTolerance) {
    if (glm::length(actual - expected) <= tolerance) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << ToString(actual) << " is not " << ToString(expected);
}

} // namespace daidalos::test
