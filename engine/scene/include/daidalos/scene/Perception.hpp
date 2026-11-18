// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/navigation/NavMesh.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <span>

namespace daidalos::scene {

/// What AI senses (ADR-0070). A sound something made: where, and how far it carries, in meters walked.
struct Noise {
    glm::vec3 position{0.0f};
    float reach = 10.0f;
    ecs::Entity maker;
    /// Seconds it has been heard for; the scene forgets it after kNoiseMemory.
    float age = 0.0f;
};
/// Seconds a noise stays in the scene, so that AI that thinks on fixed steps does not miss it.
inline constexpr float kNoiseMemory = 0.25f;

/// Lux at a point from the world's lights as they shine now (LightState), each shadowed by one ray against
/// the physics world if one is given; skip is a light left out, as the flashlight in a player's own hand.
/// An estimate of what the renderer shows: no bounced light, no profiles or cookies.
[[nodiscard]] float Illuminance(ecs::World& world, const physics::PhysicsWorld* physics, const glm::vec3& point,
                                ecs::Entity skip = {});
/// How visible lux make something, from 0 in the dark to 1 in plain light (100 lux and more).
[[nodiscard]] float Exposure(float lux);

/// Whether a listener hears a noise: when the way to it, along the navigation mesh, around walls and through
/// open doors, is within its reach; in a straight line without a mesh.
[[nodiscard]] bool Hears(const navigation::NavMesh* mesh, const glm::vec3& listener, const Noise& noise);

/// Picks among actions by their scores (ADR-0070), the way utility AI does: the highest wins, but the action
/// already chosen keeps a bonus, so that close scores do not flip it every frame.
class UtilityChooser {
public:
    explicit UtilityChooser(float momentum = 0.1f) : m_momentum(momentum) {}
    /// The index of the action chosen, which becomes the current one.
    std::size_t Choose(std::span<const float> scores);
    [[nodiscard]] std::size_t Current() const { return m_current; }

private:
    float m_momentum;
    std::size_t m_current = 0;
};

} // namespace daidalos::scene
