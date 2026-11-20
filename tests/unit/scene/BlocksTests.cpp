// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Blocks.hpp>
#include <daidalos/scene/Level.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

using namespace daidalos;
using scene::BlockShape;
using scene::LevelBlock;

namespace {

/// Every triangle's winding agrees with its vertices' normals, and the mesh stands on y = 0 within its size.
void ExpectWellMade(const resources::ModelData& model, const glm::vec3& size) {
    ASSERT_EQ(model.primitives.size(), 1U);
    ASSERT_EQ(model.nodes.size(), 1U);
    EXPECT_EQ(model.nodes[0].name, "level"); // The game takes it as level geometry.
    const resources::MeshData& mesh = model.primitives[0].mesh;
    ASSERT_EQ(mesh.indices.size() % 3, 0U);
    glm::vec3 low(1e9f);
    glm::vec3 high(-1e9f);
    for (const resources::Vertex& vertex : mesh.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    EXPECT_NEAR(low.y, 0.0f, 1e-5f);
    EXPECT_NEAR(high.y, size.y, 1e-4f);
    EXPECT_NEAR(high.x - low.x, size.x, 1e-3f);
    EXPECT_NEAR(high.z - low.z, size.z, 1e-3f);
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
        const resources::Vertex& a = mesh.vertices[mesh.indices[index]];
        const resources::Vertex& b = mesh.vertices[mesh.indices[index + 1]];
        const resources::Vertex& c = mesh.vertices[mesh.indices[index + 2]];
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
        EXPECT_GT(glm::dot(face, a.normal + b.normal + c.normal), 0.0f) << "triangle " << index / 3;
    }
}

} // namespace

TEST(BlocksTest, EveryShapeIsWellMade) {
    for (const BlockShape shape : {BlockShape::Box, BlockShape::Stairs, BlockShape::Ramp, BlockShape::Cylinder}) {
        const LevelBlock block{.shape = shape, .size = {3.0f, 1.5f, 4.0f}, .steps = 6, .sides = 12};
        SCOPED_TRACE(static_cast<int>(shape));
        ExpectWellMade(scene::MakeBlockModel(block), block.size);
    }
}

TEST(BlocksTest, StairsHaveTheirStepsAndTheColorIsTheMaterials) {
    const LevelBlock stairs{.shape = BlockShape::Stairs, .steps = 5, .color = {1.0f, 0.0f, 0.0f}};
    const resources::ModelData model = scene::MakeBlockModel(stairs);
    EXPECT_EQ(model.primitives[0].mesh.indices.size(), 5U * 6U * 6U); // Five boxes of six quads.
    EXPECT_EQ(model.materials[0].baseColorFactor, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_NE(scene::BlockKey(stairs), scene::BlockKey(LevelBlock{.shape = BlockShape::Stairs, .steps = 6}));
}

TEST(BlocksTest, BlocksKeepTheirShapeInTheLevelFile) {
    scene::Level level;
    scene::LevelEntity entity{.id = 1, .name = "Ramp"};
    entity.block = LevelBlock{.shape = BlockShape::Ramp, .size = {2.0f, 0.5f, 3.0f}, .color = {0.2f, 0.4f, 0.6f}};
    level.entities.push_back(entity);
    scene::LevelEntity cylinder{.id = 2, .name = "Pillar"};
    cylinder.block = LevelBlock{.shape = BlockShape::Cylinder, .sides = 24};
    level.entities.push_back(cylinder);
    const core::Result<scene::Level> read = scene::ParseLevel(scene::WriteLevel(level));
    ASSERT_TRUE(read.has_value()) << core::Format(read.error());
    EXPECT_EQ(*read, level);
    EXPECT_FALSE(scene::ParseLevel(R"({"entities": [{"id": 1, "block": {"shape": "dome"}}]})").has_value());
}
