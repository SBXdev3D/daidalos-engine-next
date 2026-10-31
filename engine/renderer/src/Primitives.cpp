// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/Primitives.hpp>

#include <glm/geometric.hpp>

#include <array>

namespace daidalos::renderer {

MeshData MakeCube(const glm::vec3& color) {
    constexpr std::array<glm::vec3, 6> kNormals{{
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    }};

    MeshData mesh;
    mesh.vertices.reserve(kNormals.size() * 4);
    mesh.indices.reserve(kNormals.size() * 6);
    for (const glm::vec3& normal : kNormals) {
        // Two axes in the face plane, with right x up = normal: a loop seen from outside is
        // counter-clockwise.
        const glm::vec3 up = (normal.y == 0.0f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 right = glm::cross(up, normal);

        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 center = normal * 0.5f;
        mesh.vertices.push_back({center - right * 0.5f - up * 0.5f, normal, color});
        mesh.vertices.push_back({center + right * 0.5f - up * 0.5f, normal, color});
        mesh.vertices.push_back({center + right * 0.5f + up * 0.5f, normal, color});
        mesh.vertices.push_back({center - right * 0.5f + up * 0.5f, normal, color});
        for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
            mesh.indices.push_back(first + corner);
        }
    }
    return mesh;
}

} // namespace daidalos::renderer
