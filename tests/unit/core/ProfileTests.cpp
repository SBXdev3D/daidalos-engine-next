// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Profile.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

int ProfiledFunction(int value) {
    DAI_PROFILE_FUNCTION();
    {
        DAI_PROFILE_SCOPE("Inner scope");
        value *= 2;
    }
    {
        const std::string name = "Runtime name";
        DAI_PROFILE_SCOPE_TEXT(name);
        value += 0;
    }
    return value;
}

} // namespace

// The macros must be usable as statements whether the profiler is compiled in or not;
// the *-profile presets build this test with Tracy.
TEST(Profile, MacrosDoNotChangeBehavior) {
    EXPECT_EQ(ProfiledFunction(21), 42);
    DAI_PROFILE_FRAME();
}
