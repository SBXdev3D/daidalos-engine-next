// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Particles.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace daidalos;
using renderer::kParticleCurveSamples;

namespace {

constexpr std::uint32_t kLast = kParticleCurveSamples - 1;
constexpr std::uint32_t kMiddle = kParticleCurveSamples / 2;

// The sample of the scales curves (size, emission, speed) at an index.
glm::vec4 Scales(const std::vector<glm::vec4>& baked, std::uint32_t index) {
    return baked[kParticleCurveSamples + index];
}

} // namespace

TEST(BakeParticleCurves, DefaultsFadeFromWhiteAndScaleNothing) {
    const std::vector<glm::vec4> baked = renderer::BakeParticleCurves({});
    ASSERT_EQ(baked.size(), 2U * kParticleCurveSamples);
    EXPECT_EQ(baked[0], glm::vec4(1.0f));
    EXPECT_EQ(baked[kLast], glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_EQ(Scales(baked, 0), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_EQ(Scales(baked, kLast), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
}

TEST(BakeParticleCurves, KeysAreBlendedLinearlyAndHeldBeyondTheEnds) {
    renderer::ParticleEffectDesc desc;
    desc.sizeOverLife = {{0.25f, 2.0f}, {0.75f, 4.0f}};
    const std::vector<glm::vec4> baked = renderer::BakeParticleCurves(desc);
    EXPECT_FLOAT_EQ(Scales(baked, 0).x, 2.0f);
    EXPECT_FLOAT_EQ(Scales(baked, kLast).x, 4.0f);
    const float time = static_cast<float>(kMiddle) / static_cast<float>(kLast);
    EXPECT_NEAR(Scales(baked, kMiddle).x, 2.0f + 2.0f * (time - 0.25f) / 0.5f, 1e-5f);
}

TEST(BakeParticleCurves, KeysOutOfOrderAreSortedByTime) {
    renderer::ParticleEffectDesc desc;
    desc.color = {{1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)}, {0.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}};
    const std::vector<glm::vec4> baked = renderer::BakeParticleCurves(desc);
    EXPECT_EQ(baked[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(baked[kLast], glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

TEST(BakeParticleCurves, OneKeyHoldsOverTheWholeLife) {
    renderer::ParticleEffectDesc desc;
    desc.emissionOverLife = {{0.5f, 3.0f}};
    desc.speedOverLife = {};
    const std::vector<glm::vec4> baked = renderer::BakeParticleCurves(desc);
    EXPECT_FLOAT_EQ(Scales(baked, 0).y, 3.0f);
    EXPECT_FLOAT_EQ(Scales(baked, kLast).y, 3.0f);
    EXPECT_FLOAT_EQ(Scales(baked, kMiddle).z, 1.0f) << "no keys: the speed is kept";
}
