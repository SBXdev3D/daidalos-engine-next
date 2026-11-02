// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace daidalos::physics {

/// A body of a PhysicsWorld. The default value is no body.
struct BodyId {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t value = kInvalid;

    [[nodiscard]] bool IsValid() const { return value != kInvalid; }
    bool operator==(const BodyId&) const = default;
};

/// A character of a PhysicsWorld. The default value is no character.
struct CharacterId {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t value = kInvalid;

    [[nodiscard]] bool IsValid() const { return value != kInvalid; }
    bool operator==(const CharacterId&) const = default;
};

/// Component of an entity whose LocalTransform moves a kinematic body. See SyncKinematicBodies.
struct KinematicBody {
    BodyId id;
};

struct RayHit {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
    BodyId body;
};

struct CharacterConfig {
    float radius = 0.35f;
    /// From the feet to the top of the head, in meters.
    float height = 1.85f;
    /// Steeper floors cannot be walked on.
    float maxSlopeDegrees = 50.0f;
    /// Stairs up to this height are climbed without jumping.
    float stepHeight = 0.4f;
    /// How far down the character stays stuck to the floor, for walking down stairs and slopes.
    float stickToFloor = 0.4f;
};

/// Collisions and queries of one scene, behind which Jolt is hidden (ADR-0018).
///
/// Bodies do not move by themselves here: static bodies are the level, kinematic bodies follow the
/// game, and characters are moved by their controller. All calls come from one thread, and Step runs
/// the simulation on it.
class PhysicsWorld {
public:
    PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    ~PhysicsWorld();

    /// A body made of triangles, which never moves. Indices are triangles into the positions.
    /// Fails with InvalidArgument if there is no triangle left to collide with, or if the world is full.
    [[nodiscard]] core::Result<BodyId> AddStaticMesh(std::span<const glm::vec3> positions,
                                                     std::span<const std::uint32_t> indices,
                                                     const math::Transform& transform = {});

    /// A box that only moves through MoveKinematic, and pushes characters out of its way.
    /// halfExtents are half of the size, center is where the box is relative to the body's position,
    /// and the transform's scale is ignored.
    [[nodiscard]] core::Result<BodyId> AddKinematicBox(const glm::vec3& halfExtents, const glm::vec3& center = {},
                                                       const math::Transform& transform = {});

    void RemoveBody(BodyId body);
    [[nodiscard]] std::size_t BodyCount() const;

    /// The body reaches this position and rotation at the end of the next Step, which takes that long.
    void MoveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float step);
    [[nodiscard]] glm::vec3 BodyPosition(BodyId body) const;

    /// Moves the kinematic bodies by the fixed step.
    void Step(float step);

    /// The closest hit along a unit direction. Characters are hit through their CharacterBody, and the
    /// ignored body is not hit at all: a character looking or shooting ignores its own.
    [[nodiscard]] std::optional<RayHit> CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                                BodyId ignore = {}) const;

    /// Adds a character whose feet are at position.
    [[nodiscard]] CharacterId AddCharacter(const CharacterConfig& config, const glm::vec3& position);
    void RemoveCharacter(CharacterId character);

    /// Moves the character for one fixed step, sliding along what it hits, climbing steps and staying
    /// on the floor. Its vertical speed is the game's: gravity is applied by the caller.
    void MoveCharacter(CharacterId character, const glm::vec3& velocity, float step);
    void SetCharacterPosition(CharacterId character, const glm::vec3& position);
    /// The body that follows the character, which rays hit. It collides with nothing, so
    /// characters only push each other through their own test.
    [[nodiscard]] BodyId CharacterBody(CharacterId character) const;
    /// Where the feet are.
    [[nodiscard]] glm::vec3 CharacterPosition(CharacterId character) const;
    /// The velocity left after the last move: hitting a wall or a ceiling takes speed away.
    [[nodiscard]] glm::vec3 CharacterVelocity(CharacterId character) const;
    /// True when standing on walkable ground.
    [[nodiscard]] bool IsOnGround(CharacterId character) const;
    /// Changes the height from the feet up. Returns false and changes nothing if there is no room,
    /// for example when standing up under a pipe.
    bool SetCharacterHeight(CharacterId character, float height);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// Moves the body of each entity with a KinematicBody to its LocalTransform. Those entities must be
/// at the root of the hierarchy, or under parents that do not move.
void SyncKinematicBodies(ecs::World& world, PhysicsWorld& physics, float step);

} // namespace daidalos::physics
