// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/Primitives.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

using namespace daidalos;

TEST(MakeCube, HasFourVerticesAndTwoTrianglesPerFace) {
    const renderer::MeshData cube = renderer::MakeCube(glm::vec3(1.0f));
    EXPECT_EQ(cube.vertices.size(), 24U);
    EXPECT_EQ(cube.indices.size(), 36U);
}

TEST(MakeCube, TrianglesAreCounterClockwiseFromOutside) {
    const renderer::MeshData cube = renderer::MakeCube(glm::vec3(1.0f));
    for (std::size_t index = 0; index < cube.indices.size(); index += 3) {
        const renderer::Vertex& a = cube.vertices[cube.indices[index]];
        const renderer::Vertex& b = cube.vertices[cube.indices[index + 1]];
        const renderer::Vertex& c = cube.vertices[cube.indices[index + 2]];
        const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        EXPECT_GT(glm::dot(faceNormal, a.normal), 0.0f) << "triangle " << index / 3;
    }
}

TEST(MakeCube, NormalsPointAwayFromTheCenter) {
    for (const renderer::Vertex& vertex : renderer::MakeCube(glm::vec3(1.0f)).vertices) {
        EXPECT_FLOAT_EQ(glm::dot(vertex.position, vertex.normal), 0.5f);
    }
}
