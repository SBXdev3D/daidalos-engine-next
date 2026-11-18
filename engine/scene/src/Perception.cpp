// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Perception.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace daidalos::scene {

float Illuminance(ecs::World& world, const physics::PhysicsWorld* physics, const glm::vec3& point, ecs::Entity skip) {
    float lux = 0.0f;
    world.Each<Light, ecs::WorldTransform>(
        [&](ecs::Entity entity, const Light& light, const ecs::WorldTransform& transform) {
            if (!light.enabled || entity == skip) {
                return;
            }
            const LightState* state = world.TryGet<LightState>(entity);
            const float factor = state != nullptr ? state->factor : 1.0f;
            if (factor <= 0.0f) {
                return;
            }
            const glm::vec3 axis = -glm::normalize(glm::vec3(transform.matrix[2]));
            glm::vec3 toLight;
            float distance;
            float arriving;
            if (light.type == renderer::LightType::Directional) {
                toLight = -axis;
                distance = 1000.0f;
                arriving = light.intensity;
            } else {
                const glm::vec3 offset = glm::vec3(transform.matrix[3]) - point;
                distance = glm::length(offset);
                if (distance >= light.range || distance < 1e-3f) {
                    return;
                }
                toLight = offset / distance;
                // Inverse square, faded to nothing at the range, as the renderer's window does (ADR-0043).
                const float ratio = distance / light.range;
                const float window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
                arriving = light.intensity / std::max(distance * distance, 0.01f) * window * window;
                if (light.type == renderer::LightType::Spot) {
                    const float cosine = glm::dot(-toLight, axis);
                    const float outer = std::cos(light.outerAngle);
                    const float inner = std::cos(light.innerAngle);
                    arriving *= std::clamp((cosine - outer) / std::max(inner - outer, 1e-4f), 0.0f, 1.0f);
                }
            }
            if (arriving <= 0.0f) {
                return;
            }
            // One shadow ray, stopping short of the lamp, whose own fixture would block it.
            if (physics != nullptr && physics->CastRay(point, toLight, std::max(distance - 0.3f, 0.0f)).has_value()) {
                return;
            }
            lux += arriving * factor;
        });
    return lux;
}

float Exposure(float lux) {
    return std::clamp(std::log10(std::max(lux, 0.0f) + 1.0f) / 2.0f, 0.0f, 1.0f);
}

bool Hears(const navigation::NavMesh* mesh, const glm::vec3& listener, const Noise& noise) {
    const float straight = glm::distance(listener, noise.position);
    if (straight > noise.reach) {
        return false;
    }
    if (mesh == nullptr) {
        return true;
    }
    const std::vector<glm::vec3> path = mesh->FindPath(listener, noise.position);
    if (path.empty()) {
        return straight < noise.reach * 0.25f;
    }
    // A way that stops short of the noise (a shut door) does not carry it, but for what goes through.
    if (glm::distance(path.back(), noise.position) > 1.5f) {
        return straight < noise.reach * 0.25f;
    }
    float walked = 0.0f;
    for (std::size_t index = 1; index < path.size(); ++index) {
        walked += glm::distance(path[index - 1], path[index]);
    }
    return walked <= noise.reach;
}

std::size_t UtilityChooser::Choose(std::span<const float> scores) {
    std::size_t best = m_current < scores.size() ? m_current : 0;
    float bestScore = scores.empty() ? 0.0f : scores[best] + (best == m_current ? m_momentum : 0.0f);
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] > bestScore) {
            best = index;
            bestScore = scores[index];
        }
    }
    m_current = best;
    return best;
}

} // namespace daidalos::scene
