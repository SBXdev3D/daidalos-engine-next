// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Picking.hpp"

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <limits>

using namespace daidalos;

namespace editor {

namespace {

/// The distance along the ray to the box, or nothing if it misses (slabs).
std::optional<float> HitBox(const Bounds& box, const glm::vec3& origin, const glm::vec3& direction) {
    float near = 0.0f;
    float far = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12f) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (box.min[axis] - origin[axis]) / direction[axis];
        float t1 = (box.max[axis] - origin[axis]) / direction[axis];
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far) {
            return std::nullopt;
        }
    }
    return near;
}

/// Möller and Trumbore: the distance along the ray to the triangle, both faces.
std::optional<float> HitTriangle(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& a,
                                 const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 p = glm::cross(direction, edge2);
    const float determinant = glm::dot(edge1, p);
    if (std::abs(determinant) < 1e-12f) {
        return std::nullopt;
    }
    const float inverse = 1.0f / determinant;
    const glm::vec3 s = origin - a;
    const float u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f) {
        return std::nullopt;
    }
    const glm::vec3 q = glm::cross(s, edge1);
    const float v = glm::dot(direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) {
        return std::nullopt;
    }
    const float t = glm::dot(edge2, q) * inverse;
    return t > 0.0f ? std::optional<float>(t) : std::nullopt;
}

} // namespace

Bounds Bounds::Transformed(const glm::mat4& matrix) const {
    Bounds out;
    if (Empty()) {
        return out;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) != 0 ? max.x : min.x, (corner & 2) != 0 ? max.y : min.y,
                              (corner & 4) != 0 ? max.z : min.z);
        out.Add(math::TransformPoint(matrix, point));
    }
    return out;
}

void MeshPicker::Rebuild(ecs::World& world) {
    m_meshes.clear();
    world.Each<scene::ModelInstance>([this](ecs::Entity /*entity*/, const scene::ModelInstance& placed) {
        if (placed.model == nullptr) {
            return;
        }
        for (const scene::Model::Part& part : placed.model->parts) {
            if (m_meshes.contains(part.mesh)) {
                continue;
            }
            Mesh mesh{.part = &part};
            for (const glm::vec3& position : part.positions) {
                mesh.bounds.Add(position);
            }
            m_meshes.emplace(part.mesh, mesh);
        }
    });
}

const MeshPicker::Mesh* MeshPicker::Find(renderer::MeshHandle mesh) const {
    const auto found = m_meshes.find(mesh);
    return found == m_meshes.end() ? nullptr : &found->second;
}

std::optional<PickHit> MeshPicker::Cast(ecs::World& world, const glm::vec3& origin, const glm::vec3& direction,
                                        const std::function<bool(ecs::Entity)>& skip) const {
    std::optional<PickHit> nearest;
    world.Each<scene::MeshRenderer, ecs::WorldTransform>([&](ecs::Entity entity, const scene::MeshRenderer& drawn,
                                                             const ecs::WorldTransform& transform) {
        const Mesh* mesh = Find(drawn.mesh);
        if (mesh == nullptr || (skip && skip(entity))) {
            return;
        }
        // In the mesh's own space, where its triangles are: distances along the ray stay the same.
        const glm::mat4 toLocal = glm::inverse(transform.matrix);
        const glm::vec3 localOrigin = math::TransformPoint(toLocal, origin);
        const glm::vec3 localDirection = glm::vec3(toLocal * glm::vec4(direction, 0.0f));
        const std::optional<float> boxHit = HitBox(mesh->bounds, localOrigin, localDirection);
        if (!boxHit || (nearest && *boxHit > nearest->distance)) {
            return;
        }
        const std::vector<glm::vec3>& positions = mesh->part->positions;
        const std::vector<std::uint32_t>& indices = mesh->part->indices;
        std::size_t nearestTriangle = indices.size();
        for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
            const std::optional<float> hit = HitTriangle(localOrigin, localDirection, positions[indices[index]],
                                                         positions[indices[index + 1]], positions[indices[index + 2]]);
            if (hit && (!nearest || *hit < nearest->distance)) {
                nearest = PickHit{.entity = entity, .distance = *hit, .position = origin + direction * *hit};
                nearestTriangle = index;
            }
        }
        if (nearestTriangle < indices.size()) {
            // Normals go to the world by the inverse transpose, which keeps them square to scaled surfaces.
            const glm::vec3 a = positions[indices[nearestTriangle]];
            const glm::vec3 local =
                glm::cross(positions[indices[nearestTriangle + 1]] - a, positions[indices[nearestTriangle + 2]] - a);
            glm::vec3 normal = glm::transpose(glm::mat3(toLocal)) * local;
            const float length = glm::length(normal);
            normal = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
            nearest->normal = glm::dot(normal, direction) > 0.0f ? -normal : normal;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                nearest->corners[corner] =
                    math::TransformPoint(transform.matrix, positions[indices[nearestTriangle + corner]]);
            }
        }
    });
    return nearest;
}

Bounds MeshPicker::WorldBounds(ecs::World& world, ecs::Entity entity) const {
    Bounds bounds;
    const auto add = [&](ecs::Entity drawn) {
        const scene::MeshRenderer* renderer = world.TryGet<scene::MeshRenderer>(drawn);
        const ecs::WorldTransform* transform = world.TryGet<ecs::WorldTransform>(drawn);
        if (renderer == nullptr || transform == nullptr) {
            return;
        }
        if (const Mesh* mesh = Find(renderer->mesh)) {
            bounds.Add(mesh->bounds.Transformed(transform->matrix));
        }
    };
    // The entity and what hangs from it that is not another level entity.
    std::vector<ecs::Entity> stack{entity};
    while (!stack.empty()) {
        const ecs::Entity current = stack.back();
        stack.pop_back();
        add(current);
        for (ecs::Entity child = world.FirstChild(current); !child.IsNull(); child = world.NextSibling(child)) {
            if (!world.Has<scene::LevelEntityId>(child)) {
                stack.push_back(child);
            }
        }
    }
    return bounds;
}

} // namespace editor
