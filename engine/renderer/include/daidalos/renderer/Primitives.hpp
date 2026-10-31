// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace daidalos::renderer {

/// Layout of mesh vertices.
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{1.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// A cube from -0.5 to 0.5 on each axis, with counter-clockwise front faces and hard edges.
/// Geometry for tests and samples until assets can be loaded (Phase 4).
[[nodiscard]] MeshData MakeCube(const glm::vec3& color);

} // namespace daidalos::renderer
