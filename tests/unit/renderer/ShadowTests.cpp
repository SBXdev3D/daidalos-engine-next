// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <gtest/gtest.h>

#include <vector>

#include "MeshPass.hpp"

using namespace daidalos;
using renderer::Light;
using renderer::LightType;

namespace {

Light Spot(float x, bool castsShadows = true) {
    return {.type = LightType::Spot, .position = {x, 0.0f, 0.0f}, .castsShadows = castsShadows};
}

} // namespace

TEST(ChooseShadowedLights, NearestSpotLightsThatAskForOneFirst) {
    const std::vector<Light> lights{Spot(9.0f), Spot(1.0f), Spot(5.0f), Spot(3.0f)};
    EXPECT_EQ(renderer::ChooseShadowedLights(lights, glm::vec3(0.0f), 4), (std::vector<std::uint32_t>{1, 3, 2, 0}));
}

TEST(ChooseShadowedLights, KeepsTheBudget) {
    const std::vector<Light> lights{Spot(9.0f), Spot(1.0f), Spot(5.0f), Spot(3.0f), Spot(7.0f), Spot(2.0f)};
    EXPECT_EQ(renderer::ChooseShadowedLights(lights, glm::vec3(0.0f), renderer::kMaxShadowedLights),
              (std::vector<std::uint32_t>{1, 5, 3, 2}));
}

TEST(ChooseShadowedLights, SkipsPointLightsAndLightsWithoutShadows) {
    std::vector<Light> lights{Spot(1.0f, false), Spot(2.0f), Spot(3.0f)};
    lights[2].type = LightType::Point;
    lights[2].castsShadows = true;
    EXPECT_EQ(renderer::ChooseShadowedLights(lights, glm::vec3(0.0f), 4), (std::vector<std::uint32_t>{1}));
}
