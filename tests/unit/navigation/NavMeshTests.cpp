// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/navigation/NavMesh.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace daidalos;

namespace {

// A box of six faces, as a level's walls and floors are.
void AddBox(std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices, glm::vec3 low, glm::vec3 high) {
    const auto first = static_cast<std::uint32_t>(positions.size());
    for (int corner = 0; corner < 8; ++corner) {
        positions.emplace_back((corner & 1) != 0 ? high.x : low.x, (corner & 2) != 0 ? high.y : low.y,
                               (corner & 4) != 0 ? high.z : low.z);
    }
    // Faces, wound counter-clockwise seen from outside.
    const std::uint32_t faces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                       {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (const auto& face : faces) {
        indices.insert(indices.end(), {first + face[0], first + face[1], first + face[2], first + face[0],
                                       first + face[2], first + face[3]});
    }
}

// A floor 20 m square, and a wall across it at z = 0 with a doorway from x = 6 to 8.
struct Level {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    Level() {
        AddBox(positions, indices, {-10.0f, -0.2f, -10.0f}, {10.0f, 0.0f, 10.0f});
        AddBox(positions, indices, {-10.0f, 0.0f, -0.2f}, {6.0f, 3.0f, 0.2f});
        AddBox(positions, indices, {8.0f, 0.0f, -0.2f}, {10.0f, 3.0f, 0.2f});
    }
};

float Length(const std::vector<glm::vec3>& path) {
    float length = 0.0f;
    for (std::size_t index = 1; index < path.size(); ++index) {
        length += glm::distance(path[index - 1], path[index]);
    }
    return length;
}

} // namespace

TEST(NavMeshTest, APathGoesThroughTheDoorway) {
    const Level level;
    const core::Result<navigation::NavMesh> mesh = navigation::NavMesh::Build(level.positions, level.indices);
    ASSERT_TRUE(mesh.has_value()) << core::Format(mesh.error());
    EXPECT_GT(mesh->PolygonCount(), 0u);
    const std::vector<glm::vec3> path = mesh->FindPath({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f});
    ASSERT_GE(path.size(), 3u);
    EXPECT_LT(glm::distance(path.back(), glm::vec3(0.0f, 0.0f, 5.0f)), 0.3f);
    // Around the wall: through the doorway, much longer than the 10 m straight line.
    EXPECT_GT(Length(path), 15.0f);
    bool throughTheDoorway = false;
    for (const glm::vec3& corner : path) {
        throughTheDoorway = throughTheDoorway || (corner.x > 5.5f && corner.x < 8.5f && std::abs(corner.z) < 1.0f);
    }
    EXPECT_TRUE(throughTheDoorway);
}

TEST(NavMeshTest, AShutDoorBlocksTheWay) {
    const Level level;
    const std::array<navigation::NavBox, 1> door{{{{5.8f, -1.0f, -0.6f}, {8.2f, 2.0f, 0.6f}}}};
    core::Result<navigation::NavMesh> mesh = navigation::NavMesh::Build(level.positions, level.indices, door);
    ASSERT_TRUE(mesh.has_value());
    mesh->SetBlocked({5.8f, -1.0f, -0.6f}, {8.2f, 2.0f, 0.6f}, true);
    const std::vector<glm::vec3> blocked = mesh->FindPath({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f});
    // The way stops on the near side of the wall.
    ASSERT_FALSE(blocked.empty());
    EXPECT_LT(blocked.back().z, 0.0f);
    mesh->SetBlocked({5.8f, -1.0f, -0.6f}, {8.2f, 2.0f, 0.6f}, false);
    const std::vector<glm::vec3> open = mesh->FindPath({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f});
    ASSERT_FALSE(open.empty());
    EXPECT_GT(open.back().z, 4.5f);
}

TEST(NavMeshTest, NoTrianglesIsAnError) {
    EXPECT_FALSE(navigation::NavMesh::Build({}, {}).has_value());
}
