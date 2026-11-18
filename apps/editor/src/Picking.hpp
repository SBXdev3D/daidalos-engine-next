// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/scene/AssetLibrary.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace editor {

/// A box in some space, empty while min > max.
struct Bounds {
    glm::vec3 min{1e30f};
    glm::vec3 max{-1e30f};

    [[nodiscard]] bool Empty() const { return min.x > max.x; }
    void Add(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
    void Add(const Bounds& other) {
        if (!other.Empty()) {
            Add(other.min);
            Add(other.max);
        }
    }
    /// The box around this one once transformed.
    [[nodiscard]] Bounds Transformed(const glm::mat4& matrix) const;
};

struct PickHit {
    daidalos::ecs::Entity entity;
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    /// The triangle's normal in the world, turned toward the ray's origin.
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

/// Finds what a ray from the viewport hits, on the CPU (ADR-0055): the triangles that models keep for physics,
/// tested in each drawn entity's own space, behind a box per mesh. A level of the factory's size answers in well
/// under a millisecond, which a click can afford, without reading anything back from the GPU.
class MeshPicker {
public:
    /// Learns the triangles of every model the world places (scene::ModelInstance), by mesh.
    void Rebuild(daidalos::ecs::World& world);
    /// The nearest drawn entity the ray hits, if any. direction need not be normalized.
    [[nodiscard]] std::optional<PickHit> Cast(daidalos::ecs::World& world, const glm::vec3& origin,
                                              const glm::vec3& direction) const;
    /// The box around an entity's own mesh, in world space, or empty.
    [[nodiscard]] Bounds WorldBounds(daidalos::ecs::World& world, daidalos::ecs::Entity entity) const;

private:
    struct Mesh {
        const daidalos::scene::Model::Part* part = nullptr;
        Bounds bounds;
    };
    [[nodiscard]] const Mesh* Find(daidalos::renderer::MeshHandle mesh) const;

    std::unordered_map<daidalos::renderer::MeshHandle, Mesh> m_meshes;
};

} // namespace editor
