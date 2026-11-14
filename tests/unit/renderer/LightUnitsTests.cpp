// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/LightUnits.hpp>

#include <glm/ext/scalar_constants.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace daidalos;

namespace {

float Luminance(const glm::vec3& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

} // namespace

TEST(ColorTemperature, D65IsAboutWhite) {
    // D65, the white of sRGB, is daylight: a little off the black body's curve, a few percent bluer.
    const glm::vec3 white = renderer::ColorTemperature(6504.0f);
    EXPECT_NEAR(white.r, 1.0f, 0.05f);
    EXPECT_NEAR(white.g, 1.0f, 0.05f);
    EXPECT_NEAR(white.b, 1.0f, 0.05f);
}

TEST(ColorTemperature, LowerIsWarmerAndHigherBluer) {
    const glm::vec3 bulb = renderer::ColorTemperature(2700.0f);
    EXPECT_GT(bulb.r, bulb.g);
    EXPECT_GT(bulb.g, bulb.b);
    const glm::vec3 sky = renderer::ColorTemperature(12000.0f);
    EXPECT_GT(sky.b, sky.r);
}

TEST(ColorTemperature, ChangesTheHueNotTheBrightness) {
    for (const float kelvin : {1000.0f, 1900.0f, 2700.0f, 4000.0f, 6504.0f, 10000.0f, 40000.0f}) {
        const glm::vec3 color = renderer::ColorTemperature(kelvin);
        EXPECT_NEAR(Luminance(color), 1.0f, 1e-4f) << kelvin << " K";
        EXPECT_GE(color.r, 0.0f);
        EXPECT_GE(color.g, 0.0f);
        EXPECT_GE(color.b, 0.0f);
    }
}

TEST(LightUnits, APointLightSpreadsItsLumensOverTheWholeSphere) {
    EXPECT_NEAR(renderer::PointCandela(4.0f * glm::pi<float>() * 100.0f), 100.0f, 1e-3f);
}

TEST(LightUnits, ASpotLightSpreadsItsLumensOverItsCone) {
    // A cone of 90 degrees from its axis is a half sphere: 2 pi steradians.
    EXPECT_NEAR(renderer::SpotCandela(2.0f * glm::pi<float>() * 50.0f, 0.5f * glm::pi<float>()), 50.0f, 1e-3f);
    // A narrower cone puts the same lumens into fewer steradians: it is brighter.
    EXPECT_GT(renderer::SpotCandela(1000.0f, 0.3f), renderer::SpotCandela(1000.0f, 0.6f));
}

TEST(LightUnits, ALightWithoutARangeEndsWhereItFallsTo5Centilux) {
    EXPECT_NEAR(renderer::LightRange(20.0f), 20.0f, 1e-4f);
    EXPECT_EQ(renderer::LightRange(0.0f), 1.0f);
    EXPECT_EQ(renderer::LightRange(1e6f), 64.0f);
}

TEST(LightUnits, EachExposureValueHalvesTheLight) {
    EXPECT_NEAR(renderer::ExposureScale(0.0f), 1.0f / 1.2f, 1e-6f);
    EXPECT_NEAR(renderer::ExposureScale(3.0f), 1.0f / 9.6f, 1e-6f);
    EXPECT_NEAR(renderer::ExposureScale(4.0f) * 2.0f, renderer::ExposureScale(3.0f), 1e-6f);
}
