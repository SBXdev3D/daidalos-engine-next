// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Perception.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace daidalos;

namespace {

ecs::Entity AddLight(ecs::World& world, const scene::Light& light, const glm::vec3& position,
                     const glm::vec3& pointing = {0.0f, -1.0f, 0.0f}) {
    const ecs::Entity entity = world.Create();
    ecs::LocalTransform& transform = world.Add<ecs::LocalTransform>(entity);
    transform.translation = position;
    // The light points down its -Z.
    transform.rotation = glm::quatLookAtRH(pointing, glm::vec3(0.0f, 0.0f, 1.0f));
    world.Add<scene::Light>(entity, light);
    return entity;
}

void AddBox(std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices, glm::vec3 low, glm::vec3 high) {
    const auto first = static_cast<std::uint32_t>(positions.size());
    for (int corner = 0; corner < 8; ++corner) {
        positions.emplace_back((corner & 1) != 0 ? high.x : low.x, (corner & 2) != 0 ? high.y : low.y,
                               (corner & 4) != 0 ? high.z : low.z);
    }
    const std::uint32_t faces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                       {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (const auto& face : faces) {
        indices.insert(indices.end(), {first + face[0], first + face[1], first + face[2], first + face[0],
                                       first + face[2], first + face[3]});
    }
}

} // namespace

TEST(PerceptionTest, APointLightFallsOffWithTheSquareOfTheDistance) {
    ecs::World world;
    AddLight(world, {.intensity = 100.0f, .range = 100.0f}, {0.0f, 2.0f, 0.0f});
    ecs::PropagateTransforms(world);
    // 100 candela 2 m away: 25 lux, all but the window's last bit.
    EXPECT_NEAR(scene::Illuminance(world, nullptr, glm::vec3(0.0f)), 25.0f, 0.1f);
    EXPECT_NEAR(scene::Illuminance(world, nullptr, {0.0f, -2.0f, 0.0f}), 100.0f / 16.0f, 0.1f);
}

TEST(PerceptionTest, ASpotLightsOnlyItsCone) {
    ecs::World world;
    AddLight(world, {.type = renderer::LightType::Spot, .intensity = 100.0f, .range = 100.0f}, {0.0f, 2.0f, 0.0f});
    ecs::PropagateTransforms(world);
    EXPECT_GT(scene::Illuminance(world, nullptr, glm::vec3(0.0f)), 20.0f);
    EXPECT_EQ(scene::Illuminance(world, nullptr, {5.0f, 0.0f, 0.0f}), 0.0f);
}

TEST(PerceptionTest, ADimmedOrSkippedLightGivesLess) {
    ecs::World world;
    const ecs::Entity lamp = AddLight(world, {.intensity = 100.0f, .range = 100.0f}, {0.0f, 2.0f, 0.0f});
    ecs::PropagateTransforms(world);
    world.Add<scene::LightState>(lamp).factor = 0.5f;
    EXPECT_NEAR(scene::Illuminance(world, nullptr, glm::vec3(0.0f)), 12.5f, 0.1f);
    EXPECT_EQ(scene::Illuminance(world, nullptr, glm::vec3(0.0f), lamp), 0.0f);
}

TEST(PerceptionTest, ExposureRisesWithLight) {
    EXPECT_EQ(scene::Exposure(0.0f), 0.0f);
    EXPECT_LT(scene::Exposure(1.0f), scene::Exposure(10.0f));
    EXPECT_EQ(scene::Exposure(1000.0f), 1.0f);
}

TEST(PerceptionTest, NoiseIsHeardAroundTheWallNotThroughIt) {
    // A floor and a wall with a doorway far to the side: a noise just behind the wall is near in a straight
    // line, but the way around is long.
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    AddBox(positions, indices, {-10.0f, -0.2f, -10.0f}, {10.0f, 0.0f, 10.0f});
    AddBox(positions, indices, {-10.0f, 0.0f, -0.2f}, {6.0f, 3.0f, 0.2f});
    AddBox(positions, indices, {8.0f, 0.0f, -0.2f}, {10.0f, 3.0f, 0.2f});
    const core::Result<navigation::NavMesh> mesh = navigation::NavMesh::Build(positions, indices);
    ASSERT_TRUE(mesh.has_value());
    const scene::Noise step{.position = {0.0f, 0.0f, 1.0f}, .reach = 6.0f};
    EXPECT_TRUE(scene::Hears(nullptr, {0.0f, 0.0f, -1.0f}, step));
    EXPECT_FALSE(scene::Hears(&*mesh, {0.0f, 0.0f, -1.0f}, step));
    const scene::Noise shot{.position = {0.0f, 0.0f, 1.0f}, .reach = 30.0f};
    EXPECT_TRUE(scene::Hears(&*mesh, {0.0f, 0.0f, -1.0f}, shot));
}

TEST(PerceptionTest, TheChoiceKeepsItsActionUntilAnotherIsClearlyBetter) {
    scene::UtilityChooser chooser(0.1f);
    EXPECT_EQ(chooser.Choose(std::array{0.5f, 0.2f}), 0u);
    EXPECT_EQ(chooser.Choose(std::array{0.5f, 0.55f}), 0u);
    EXPECT_EQ(chooser.Choose(std::array{0.5f, 0.7f}), 1u);
}
