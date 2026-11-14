// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

using namespace daidalos;
using namespace daidalos::physics;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kGravity = 7.0f;

/// The demo's level, as the game sees it: every part of the "level" node collides.
class FactoryLevel : public ::testing::Test {
protected:
    void SetUp() override {
        core::Result<resources::ModelData> model = resources::LoadModelFile(DAI_FACTORY_LEVEL);
        ASSERT_TRUE(model.has_value()) << core::Format(model.error());
        for (const resources::ModelNode& node : model->nodes) {
            if (node.name == "player") {
                m_start = node.local.translation;
            }
            if (node.name != "level") {
                continue;
            }
            for (const std::uint32_t index : node.primitives) {
                const resources::MeshData& mesh = model->primitives[index].mesh;
                std::vector<glm::vec3> positions;
                positions.reserve(mesh.vertices.size());
                for (const resources::Vertex& vertex : mesh.vertices) {
                    positions.push_back(vertex.position);
                }
                ASSERT_TRUE(m_physics.AddStaticMesh(positions, mesh.indices).has_value());
            }
        }
        m_physics.Step(kStep);
    }

    void Settle(CharacterHandle character) {
        for (int step = 0; step < 180; ++step) {
            const float fall =
                m_physics.IsOnGround(character) ? 0.0f : m_physics.CharacterVelocity(character).y - kGravity * kStep;
            m_physics.MoveCharacter(character, {0.0f, fall, 0.0f}, kStep);
        }
    }

    PhysicsWorld m_physics;
    glm::vec3 m_start{0.0f};
};

} // namespace

TEST_F(FactoryLevel, PlayerStandsOnTheFloorAtTheStart) {
    const CharacterHandle player = m_physics.AddCharacter({}, m_start);
    Settle(player);
    EXPECT_TRUE(m_physics.IsOnGround(player));
    EXPECT_GT(m_physics.CharacterPosition(player).y, -1.0f) << "The player fell through the floor";
}

TEST_F(FactoryLevel, WallsStopTheViewBeforeTheEndOfTheLevel) {
    // Every direction from the start hits something: the level is closed.
    for (const glm::vec3 direction : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
                                      glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)}) {
        EXPECT_TRUE(m_physics.CastRay(m_start, direction, 500.0f).has_value());
    }
}
