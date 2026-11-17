// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Primitives.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace daidalos::renderer {

resources::MeshData MakeCube() {
    constexpr std::array<glm::vec3, 6> kNormals{{
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    }};

    resources::MeshData mesh;
    mesh.vertices.reserve(kNormals.size() * 4);
    mesh.indices.reserve(kNormals.size() * 6);
    for (const glm::vec3& normal : kNormals) {
        // Two axes in the face plane, with right x up = normal: a loop seen from outside is
        // counter-clockwise.
        const glm::vec3 up = (normal.y == 0.0f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 right = glm::cross(up, normal);

        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 center = normal * 0.5f;
        const glm::vec4 tangent(right, 1.0f);
        mesh.vertices.push_back({center - right * 0.5f - up * 0.5f, normal, tangent, {0.0f, 1.0f}});
        mesh.vertices.push_back({center + right * 0.5f - up * 0.5f, normal, tangent, {1.0f, 1.0f}});
        mesh.vertices.push_back({center + right * 0.5f + up * 0.5f, normal, tangent, {1.0f, 0.0f}});
        mesh.vertices.push_back({center - right * 0.5f + up * 0.5f, normal, tangent, {0.0f, 0.0f}});
        for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
            mesh.indices.push_back(first + corner);
        }
    }
    return mesh;
}

resources::MeshData MakeSphere(std::uint32_t segments, std::uint32_t rings) {
    constexpr float kPi = 3.14159265358979f;
    segments = std::max(segments, 3U);
    rings = std::max(rings, 2U);
    resources::MeshData mesh;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float theta = v * kPi; // From the top pole.
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float phi = u * 2.0f * kPi;
            const glm::vec3 normal{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
            resources::Vertex vertex;
            vertex.position = normal * 0.5f;
            vertex.normal = normal;
            // Along increasing u, around the axis.
            vertex.tangent = glm::vec4(-std::sin(phi), 0.0f, -std::cos(phi), 1.0f);
            vertex.uv = {u, v};
            mesh.vertices.push_back(vertex);
        }
    }
    const std::uint32_t row = segments + 1;
    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t a = ring * row + segment;
            const std::uint32_t b = a + row;
            // Counter-clockwise seen from outside.
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return mesh;
}

} // namespace daidalos::renderer
