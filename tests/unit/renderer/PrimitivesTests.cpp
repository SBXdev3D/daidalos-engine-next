// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Primitives.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

using namespace daidalos;

TEST(MakeCube, HasFourVerticesAndTwoTrianglesPerFace) {
    const resources::MeshData cube = renderer::MakeCube();
    EXPECT_EQ(cube.vertices.size(), 24U);
    EXPECT_EQ(cube.indices.size(), 36U);
}

TEST(MakeCube, TrianglesAreCounterClockwiseFromOutside) {
    const resources::MeshData cube = renderer::MakeCube();
    for (std::size_t index = 0; index < cube.indices.size(); index += 3) {
        const resources::Vertex& a = cube.vertices[cube.indices[index]];
        const resources::Vertex& b = cube.vertices[cube.indices[index + 1]];
        const resources::Vertex& c = cube.vertices[cube.indices[index + 2]];
        const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        EXPECT_GT(glm::dot(faceNormal, a.normal), 0.0f) << "triangle " << index / 3;
    }
}

TEST(MakeCube, NormalsPointAwayFromTheCenter) {
    for (const resources::Vertex& vertex : renderer::MakeCube().vertices) {
        EXPECT_FLOAT_EQ(glm::dot(vertex.position, vertex.normal), 0.5f);
    }
}

TEST(MakeSphere, IsClosedAroundItsCenterWithOutwardTriangles) {
    const resources::MeshData sphere = renderer::MakeSphere(16, 8);
    ASSERT_EQ(sphere.indices.size(), 16U * 8U * 6U);
    for (const resources::Vertex& vertex : sphere.vertices) {
        EXPECT_NEAR(glm::length(vertex.position), 0.5f, 1e-5f);
    }
    // Every triangle with an area faces away from the center.
    for (std::size_t index = 0; index + 2 < sphere.indices.size(); index += 3) {
        const glm::vec3 a = sphere.vertices[sphere.indices[index]].position;
        const glm::vec3 b = sphere.vertices[sphere.indices[index + 1]].position;
        const glm::vec3 c = sphere.vertices[sphere.indices[index + 2]].position;
        const glm::vec3 normal = glm::cross(b - a, c - a);
        if (glm::length(normal) > 1e-6f) {
            EXPECT_GT(glm::dot(normal, a + b + c), 0.0f);
        }
    }
}
