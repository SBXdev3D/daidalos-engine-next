// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/LightProfile.hpp>

#include <gtest/gtest.h>

#include <string>

using namespace daidalos;

namespace {

// A downlight: 1000 cd straight down, half of it at 45 degrees, none from 90.
constexpr const char* kDownlight = R"(IESNA:LM-63-2002
[TEST] unit
TILT=NONE
1 -1 2 3 1 1 2 0 0 0
1 1 0
0 45 90
0
500 250 0
)";

std::size_t SampleAt(float degrees) {
    return static_cast<std::size_t>(degrees / 180.0f * static_cast<float>(resources::LightProfile::kSamples - 1) +
                                    0.5f);
}

TEST(LightProfile, ReadsTheIntensityByAngleFromTheAxis) {
    const core::Result<resources::LightProfile> profile = resources::ParseIes(kDownlight);
    ASSERT_TRUE(profile.has_value()) << core::Format(profile.error());
    ASSERT_EQ(profile->samples.size(), resources::LightProfile::kSamples);
    EXPECT_FLOAT_EQ(profile->peakCandela, 1000.0f) << "Times the multiplier";
    EXPECT_FLOAT_EQ(profile->samples[0], 1.0f);
    EXPECT_NEAR(profile->samples[SampleAt(45.0f)], 0.5f, 0.02f);
    EXPECT_NEAR(profile->samples[SampleAt(67.5f)], 0.25f, 0.03f) << "Between the angles given, linearly";
    EXPECT_FLOAT_EQ(profile->samples[SampleAt(135.0f)], 0.0f) << "Past the last angle, no light";
}

TEST(LightProfile, AveragesAroundTheAxis) {
    const std::string text = "TILT=NONE\n1 -1 1 2 2 1 2 0 0 0\n1 1 0\n0 180\n0 90\n100 100\n300 300\n";
    const core::Result<resources::LightProfile> profile = resources::ParseIes(text);
    ASSERT_TRUE(profile.has_value()) << core::Format(profile.error());
    EXPECT_FLOAT_EQ(profile->peakCandela, 200.0f);
    EXPECT_FLOAT_EQ(profile->samples.back(), 1.0f) << "A profile to 180 degrees holds to the end";
}

TEST(LightProfile, SkipsATiltTable) {
    const std::string text = "TILT=INCLUDE\n1\n2\n0 90\n1 0.5\n1 -1 1 2 1 1 2 0 0 0\n1 1 0\n0 90\n0\n10 5\n";
    const core::Result<resources::LightProfile> profile = resources::ParseIes(text);
    ASSERT_TRUE(profile.has_value()) << core::Format(profile.error());
    EXPECT_FLOAT_EQ(profile->peakCandela, 10.0f);
}

TEST(LightProfile, RefusesWhatItCannotRead) {
    EXPECT_FALSE(resources::ParseIes("no tilt here").has_value());
    EXPECT_FALSE(resources::ParseIes("TILT=NONE\n1 -1 1 3 1 1 2 0 0 0\n1 1 0\n0 45\n").has_value()) << "Short";
    EXPECT_FALSE(resources::ParseIes("TILT=NONE\n1 -1 1 2 1 2 2 0 0 0\n1 1 0\n0 90\n0\n1 1\n").has_value()) << "Type B";
    EXPECT_FALSE(resources::ParseIes("TILT=NONE\n1 -1 1 2 1 1 2 0 0 0\n1 1 0\n0 90\n0\n0 0\n").has_value())
        << "No light";
    EXPECT_FALSE(resources::LoadIesFile("no-such-profile.ies").has_value());
}

} // namespace
