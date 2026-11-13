// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "LightSampling.hpp"

namespace {

using daidalos::renderer::AliasEntry;
using daidalos::renderer::Light;
using daidalos::renderer::LightType;

// Draws a million indices and compares their counts with the table's probabilities: chi-squared, with a
// threshold far above what the degrees of freedom give by chance.
void ExpectDrawsFollowTheWeights(const std::vector<float>& weights) {
    const std::vector<AliasEntry> table = daidalos::renderer::BuildAliasTable(weights);
    ASSERT_EQ(table.size(), weights.size());
    double total = 0.0;
    for (const float weight : weights) {
        total += weight;
    }
    std::mt19937 random(1234);
    std::uniform_real_distribution<float> uniform(0.0f, std::nextafter(1.0f, 0.0f));
    constexpr int kDraws = 1'000'000;
    std::vector<int> counts(weights.size(), 0);
    for (int draw = 0; draw < kDraws; ++draw) {
        const float pick = uniform(random);
        const float keep = uniform(random);
        ++counts[daidalos::renderer::SampleAliasTable(table, pick, keep)];
    }
    double chiSquared = 0.0;
    int degrees = -1;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        const double expected = weights[index] / total;
        EXPECT_NEAR(table[index].probability, expected, 1e-6) << "entry " << index;
        if (expected == 0.0) {
            EXPECT_EQ(counts[index], 0) << "entry " << index << " has no weight";
            continue;
        }
        const double difference = counts[index] - expected * kDraws;
        chiSquared += difference * difference / (expected * kDraws);
        ++degrees;
    }
    // Far in the tail for these degrees of freedom: a wrong table gives thousands.
    EXPECT_LT(chiSquared, 3.0 * degrees + 30.0);
}

TEST(AliasTable, DrawsEachEntryWithItsProbability) {
    ExpectDrawsFollowTheWeights({1.0f, 2.0f, 3.0f, 4.0f});
}

TEST(AliasTable, KeepsRareAndDominantEntries) {
    ExpectDrawsFollowTheWeights({1000.0f, 0.5f, 1.0f, 0.0f, 3.0f, 0.01f, 250.0f});
}

TEST(AliasTable, ManyEntriesOfRandomWeights) {
    std::mt19937 random(99);
    std::uniform_real_distribution<float> weight(0.0f, 10.0f);
    std::vector<float> weights(1000);
    for (float& entry : weights) {
        entry = weight(random);
    }
    ExpectDrawsFollowTheWeights(weights);
}

TEST(AliasTable, WithoutAnyWeightEveryEntryIsAsLikely) {
    const std::vector<AliasEntry> table = daidalos::renderer::BuildAliasTable(std::vector<float>{0.0f, 0.0f});
    ASSERT_EQ(table.size(), 2U);
    EXPECT_FLOAT_EQ(table[0].probability, 0.5f);
    EXPECT_FLOAT_EQ(table[1].probability, 0.5f);
    EXPECT_TRUE(daidalos::renderer::BuildAliasTable({}).empty());
}

TEST(LightPower, PointLightSendsItsIntensityEverywhere) {
    const Light light{.type = LightType::Point, .color = glm::vec3(100.0f)};
    EXPECT_NEAR(daidalos::renderer::LightPower(light), 4.0f * std::numbers::pi_v<float> * 100.0f, 1e-2f);
}

TEST(LightPower, SpotLightSendsItThroughItsCone) {
    const float pi = std::numbers::pi_v<float>;
    // A cone of half a turn is half the sphere.
    const Light half{.type = LightType::Spot, .color = glm::vec3(100.0f), .innerAngle = pi / 2, .outerAngle = pi / 2};
    EXPECT_NEAR(daidalos::renderer::LightPower(half), 2.0f * pi * 100.0f, 1e-2f);
    const Light narrow{.type = LightType::Spot, .color = glm::vec3(100.0f), .innerAngle = 0.1f, .outerAngle = 0.3f};
    EXPECT_NEAR(daidalos::renderer::LightPower(narrow), 2.0f * pi * (1.0f - std::cos(0.2f)) * 100.0f, 1e-3f);
}

TEST(LightPower, DirectionalLightCountsWhatItSendsThroughADisk) {
    const Light moon{.type = LightType::Directional, .color = glm::vec3(0.25f)};
    const float reach = daidalos::renderer::kDirectionalReach;
    EXPECT_NEAR(daidalos::renderer::LightPower(moon), std::numbers::pi_v<float> * reach * reach * 0.25f, 1e-3f);
}

TEST(LightPower, FollowsTheLuminanceOfTheColor) {
    const Light green{.color = glm::vec3(0.0f, 1.0f, 0.0f)};
    const Light blue{.color = glm::vec3(0.0f, 0.0f, 1.0f)};
    EXPECT_GT(daidalos::renderer::LightPower(green), 9.0f * daidalos::renderer::LightPower(blue));
}

using daidalos::renderer::kGone;
using daidalos::renderer::LightRange;

TEST(MapLights, FollowsLightsThatMoveInTheList) {
    const std::array<LightRange, 3> previous{{{10, 0, 1}, {20, 1, 1}, {30, 2, 1}}};
    const std::array<LightRange, 4> current{{{30, 0, 1}, {40, 1, 1}, {10, 2, 1}, {20, 3, 1}}};
    EXPECT_EQ(daidalos::renderer::MapLights(previous, current, 3), (std::vector<std::uint32_t>{2, 3, 0}));
}

TEST(MapLights, LightsThatAreGoneMapToNothing) {
    const std::array<LightRange, 3> previous{{{10, 0, 1}, {20, 1, 1}, {30, 2, 1}}};
    const std::array<LightRange, 1> current{{{20, 0, 1}}};
    EXPECT_EQ(daidalos::renderer::MapLights(previous, current, 3), (std::vector<std::uint32_t>{kGone, 0, kGone}));
}

TEST(MapLights, TheTrianglesOfADrawFollowIt) {
    // A lamp, then an emissive draw of three triangles; next frame, a new lamp comes first.
    const std::array<LightRange, 2> previous{{{10, 0, 1}, {77, 1, 3}}};
    const std::array<LightRange, 3> current{{{5, 0, 1}, {10, 1, 1}, {77, 2, 3}}};
    EXPECT_EQ(daidalos::renderer::MapLights(previous, current, 4), (std::vector<std::uint32_t>{1, 2, 3, 4}));
}

TEST(MapLights, ADrawWhoseTrianglesChangedStartsOver) {
    const std::array<LightRange, 1> previous{{{77, 0, 3}}};
    const std::array<LightRange, 1> current{{{77, 0, 2}}};
    EXPECT_EQ(daidalos::renderer::MapLights(previous, current, 3), (std::vector<std::uint32_t>(3, kGone)));
}

TEST(IdentityKey, LightsWithoutAnIdAreNamedByTheirPosition) {
    using daidalos::renderer::IdentityKey;
    EXPECT_EQ(IdentityKey(7, 3), 7U);
    EXPECT_EQ(IdentityKey(0, 3), IdentityKey(0, 3));
    EXPECT_NE(IdentityKey(0, 3), IdentityKey(0, 4));
    EXPECT_NE(IdentityKey(0, 3), 3U);
}

} // namespace
