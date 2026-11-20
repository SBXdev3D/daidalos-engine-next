// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Blocks.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace daidalos::scene {

namespace {

/// Adds a flat quad, corners counter-clockwise seen from where normal points, its texture laid in meters along
/// its own edges.
void Quad(resources::MeshData& mesh, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
    const glm::vec3 across = glm::normalize(b - a);
    const glm::vec3 up = glm::cross(normal, across);
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const glm::vec3& corner : {a, b, c, d}) {
        const glm::vec3 offset = corner - a;
        mesh.vertices.push_back({.position = corner,
                                 .normal = normal,
                                 .tangent = glm::vec4(across, 1.0f),
                                 .uv = glm::vec2(glm::dot(offset, across), glm::dot(offset, up))});
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

/// A box from low to high.
void Box(resources::MeshData& mesh, const glm::vec3& low, const glm::vec3& high) {
    const auto at = [&](int x, int y, int z) {
        return glm::vec3(x != 0 ? high.x : low.x, y != 0 ? high.y : low.y, z != 0 ? high.z : low.z);
    };
    Quad(mesh, at(0, 0, 1), at(1, 0, 1), at(1, 1, 1), at(0, 1, 1)); // +Z
    Quad(mesh, at(1, 0, 0), at(0, 0, 0), at(0, 1, 0), at(1, 1, 0)); // -Z
    Quad(mesh, at(1, 0, 1), at(1, 0, 0), at(1, 1, 0), at(1, 1, 1)); // +X
    Quad(mesh, at(0, 0, 0), at(0, 0, 1), at(0, 1, 1), at(0, 1, 0)); // -X
    Quad(mesh, at(0, 1, 1), at(1, 1, 1), at(1, 1, 0), at(0, 1, 0)); // +Y
    Quad(mesh, at(0, 0, 0), at(1, 0, 0), at(1, 0, 1), at(0, 0, 1)); // -Y
}

void Triangle(resources::MeshData& mesh, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    const glm::vec3 across = glm::normalize(b - a);
    const glm::vec3 up = glm::cross(normal, across);
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const glm::vec3& corner : {a, b, c}) {
        const glm::vec3 offset = corner - a;
        mesh.vertices.push_back({.position = corner,
                                 .normal = normal,
                                 .tangent = glm::vec4(across, 1.0f),
                                 .uv = glm::vec2(glm::dot(offset, across), glm::dot(offset, up))});
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

} // namespace

resources::ModelData MakeBlockModel(const LevelBlock& block) {
    const glm::vec3 size = glm::max(block.size, glm::vec3(0.01f));
    const glm::vec3 half(size.x * 0.5f, 0.0f, size.z * 0.5f);
    resources::MeshData mesh;
    switch (block.shape) {
    case BlockShape::Box:
        Box(mesh, -half, glm::vec3(half.x, size.y, half.z));
        break;
    case BlockShape::Stairs: {
        // Steps rising towards -Z, each a box from the floor up to its tread.
        const std::uint32_t steps = std::clamp(block.steps, 1U, 64U);
        for (std::uint32_t step = 0; step < steps; ++step) {
            const float front = half.z - size.z * static_cast<float>(step) / static_cast<float>(steps);
            const float back = half.z - size.z * static_cast<float>(step + 1) / static_cast<float>(steps);
            Box(mesh, glm::vec3(-half.x, 0.0f, back),
                glm::vec3(half.x, size.y * static_cast<float>(step + 1) / static_cast<float>(steps), front));
        }
        break;
    }
    case BlockShape::Ramp: {
        // A wedge: its floor, its slope up towards -Z, its upright back, and its two sides.
        const glm::vec3 frontLeft(-half.x, 0.0f, half.z);
        const glm::vec3 frontRight(half.x, 0.0f, half.z);
        const glm::vec3 backLeft(-half.x, 0.0f, -half.z);
        const glm::vec3 backRight(half.x, 0.0f, -half.z);
        const glm::vec3 topLeft(-half.x, size.y, -half.z);
        const glm::vec3 topRight(half.x, size.y, -half.z);
        Quad(mesh, backLeft, backRight, frontRight, frontLeft); // Floor, facing down.
        Quad(mesh, frontLeft, frontRight, topRight, topLeft);   // Slope.
        Quad(mesh, backRight, backLeft, topLeft, topRight);     // Back.
        Triangle(mesh, frontRight, backRight, topRight);        // +X side.
        Triangle(mesh, backLeft, frontLeft, topLeft);           // -X side.
        break;
    }
    case BlockShape::Cylinder: {
        // Upright, its round sides smooth, across X and Z as the size says.
        const std::uint32_t sides = std::clamp(block.sides, 3U, 128U);
        const auto point = [&](std::uint32_t side, float y) {
            const float angle = glm::two_pi<float>() * static_cast<float>(side) / static_cast<float>(sides);
            return glm::vec3(std::cos(angle) * half.x, y, -std::sin(angle) * half.z);
        };
        const float around = glm::two_pi<float>() * (half.x + half.z) * 0.5f;
        for (std::uint32_t side = 0; side < sides; ++side) {
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const std::uint32_t corner : {side, side + 1}) {
                const float angle = glm::two_pi<float>() * static_cast<float>(corner) / static_cast<float>(sides);
                const glm::vec3 normal =
                    glm::normalize(glm::vec3(std::cos(angle) / half.x, 0.0f, -std::sin(angle) / half.z));
                const glm::vec4 tangent(glm::normalize(glm::vec3(-std::sin(angle), 0.0f, -std::cos(angle))), 1.0f);
                const float u = around * static_cast<float>(corner) / static_cast<float>(sides);
                mesh.vertices.push_back(
                    {.position = point(corner, 0.0f), .normal = normal, .tangent = tangent, .uv = {u, 0.0f}});
                mesh.vertices.push_back(
                    {.position = point(corner, size.y), .normal = normal, .tangent = tangent, .uv = {u, size.y}});
            }
            mesh.indices.insert(mesh.indices.end(), {first, first + 2, first + 3, first, first + 3, first + 1});
            Triangle(mesh, glm::vec3(0.0f, size.y, 0.0f), point(side, size.y), point(side + 1, size.y));
            Triangle(mesh, glm::vec3(0.0f), point(side + 1, 0.0f), point(side, 0.0f));
        }
        break;
    }
    }
    resources::ModelData model;
    model.primitives.push_back({.mesh = std::move(mesh), .material = 0});
    resources::MaterialData material;
    material.name = "Block";
    material.baseColorFactor = glm::vec4(glm::clamp(block.color, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 0.85f;
    material.surface = "concrete";
    model.materials.push_back(material);
    model.nodes.push_back({.name = "level", .primitives = {0}});
    return model;
}

std::string BlockKey(const LevelBlock& block) {
    return std::format("block:{}:{}x{}x{}:{}:{}:{},{},{}", static_cast<int>(block.shape), block.size.x, block.size.y,
                       block.size.z, block.steps, block.sides, block.color.r, block.color.g, block.color.b);
}

} // namespace daidalos::scene
