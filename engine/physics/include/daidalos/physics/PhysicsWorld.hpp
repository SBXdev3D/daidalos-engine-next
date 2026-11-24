// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/core/Result.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace daidalos::physics {

/// A body of a PhysicsWorld. The default value is no body.
struct BodyId {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t value = kInvalid;

    [[nodiscard]] bool IsValid() const { return value != kInvalid; }
    bool operator==(const BodyId&) const = default;
};

struct CharacterTag;
/// A character of a PhysicsWorld. The default value is no character.
using CharacterHandle = core::Handle<CharacterTag>;

struct RayHit {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
    BodyId body;
};

/// The shape of a dynamic body (ADR-0125). Each fits in the box of size around center, in the body's frame: a
/// sphere's diameter is the box's smallest side, a cylinder and a capsule stand along Y, as tall as the box and as
/// wide as its narrower side across, and a hull wraps its points, which the box does not limit.
enum class ShapeKind : std::uint8_t { Box, Sphere, Cylinder, Capsule, Hull };

struct BodyShape {
    ShapeKind kind = ShapeKind::Box;
    glm::vec3 size{0.5f};
    glm::vec3 center{0.0f};
    /// The hull's points, in the body's frame: of many, only those furthest out along 64 directions are kept.
    std::vector<glm::vec3> points;
};

/// How a body meets others (ADR-0125). Two bodies touching rub with the square root of the product of their
/// frictions, and bounce with the larger of their restitutions. Damping is the share of speed lost each second.
struct BodyMaterial {
    float friction = 0.6f;
    float restitution = 0.1f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool operator==(const BodyMaterial&) const = default;
};

struct DynamicBodyDesc {
    BodyShape shape;
    /// Kilograms.
    float mass = 10.0f;
    BodyMaterial material;
    /// Swept every step against what it could pass through: for small things that fly fast, such as debris.
    bool fast = false;
};

/// Two bodies touching (ADR-0125), at least one of them dynamic. Collected during Step.
struct Contact {
    BodyId first;
    BodyId second;
    glm::vec3 point{0.0f};
    /// From the second body towards the first.
    glm::vec3 normal{0.0f};
    /// How fast they were closing along the normal when they met, in meters per second; 0 for a touch that
    /// goes on.
    float approach = 0.0f;
    /// How fast their surfaces slide past each other at the point, in meters per second: a rolling body
    /// barely slides.
    float slide = 0.0f;
    /// They met in this step; false for a touch that goes on from an earlier one.
    bool fresh = true;
};

/// The edges of a body's shape where it is, for debug views.
struct BodyOutline {
    BodyId body;
    bool dynamic = false;
    bool sleeping = false;
    /// Pairs of points, in world space.
    std::vector<glm::vec3> lines;
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
    /// Only the material's friction and restitution matter to a body that does not move.
    [[nodiscard]] core::Result<BodyId> AddStaticMesh(std::span<const glm::vec3> positions,
                                                     std::span<const std::uint32_t> indices,
                                                     const math::Transform& transform = {},
                                                     const BodyMaterial& material = {0.2f, 0.0f, 0.0f, 0.0f});

    /// A box that only moves through MoveKinematic, and pushes characters out of its way.
    /// halfExtents are half of the size, center is where the box is relative to the body's position,
    /// and the transform's scale is ignored.
    [[nodiscard]] core::Result<BodyId> AddKinematicBox(const glm::vec3& halfExtents, const glm::vec3& center = {},
                                                       const math::Transform& transform = {});

    /// A box that falls, slides, tumbles and can be pushed (ADR-0073): mass in kilograms, half extents in meters,
    /// around a center in its own frame.
    [[nodiscard]] core::Result<BodyId> AddDynamicBox(const glm::vec3& halfExtents, float mass,
                                                     const math::Transform& transform, const glm::vec3& center = {});
    /// A body of any shape that falls, rolls, slides and tumbles (ADR-0125). The transform's scale is ignored:
    /// the shape is given at its size. Fails with InvalidArgument for a shape Jolt cannot make, such as a hull
    /// of fewer than four points that are not on one plane.
    [[nodiscard]] core::Result<BodyId> AddDynamicBody(const DynamicBodyDesc& desc, const math::Transform& transform);
    [[nodiscard]] glm::quat BodyRotation(BodyId body) const;
    [[nodiscard]] glm::vec3 BodyVelocity(BodyId body) const;
    /// Radians per second, around the axis it points along.
    [[nodiscard]] glm::vec3 BodyAngularVelocity(BodyId body) const;
    /// Sets how a dynamic body moves and turns, waking it.
    void SetBodyVelocity(BodyId body, const glm::vec3& linear, const glm::vec3& angular = {});
    /// Puts a body somewhere at once, as a saved game does, waking it.
    void SetBodyTransform(BodyId body, const glm::vec3& position, const glm::quat& rotation);
    /// A push through the body's center of mass, in kilogram-meters per second.
    void AddImpulse(BodyId body, const glm::vec3& impulse);
    /// A push at a point in the world: off the center of mass, it also turns the body. Wakes it.
    void AddImpulseAt(BodyId body, const glm::vec3& impulse, const glm::vec3& point);
    /// Asleep: it has come to rest, and the simulation leaves it until something touches or pushes it.
    [[nodiscard]] bool IsSleeping(BodyId body) const;
    /// The dynamic bodies whose shapes reach into a sphere, in no set order.
    [[nodiscard]] std::vector<BodyId> DynamicBodiesInSphere(const glm::vec3& center, float radius) const;
    [[nodiscard]] bool IsDynamic(BodyId body) const;
    [[nodiscard]] float BodyMass(BodyId body) const;
    void RemoveBody(BodyId body);
    [[nodiscard]] std::size_t BodyCount() const;

    /// The body reaches this position and rotation at the end of the next Step, which takes that long.
    void MoveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float step);
    [[nodiscard]] glm::vec3 BodyPosition(BodyId body) const;

    /// Moves the kinematic bodies by the fixed step, and the dynamic ones under gravity and what they touch.
    void Step(float step);
    /// What touched during the last Step (ADR-0125): new contacts, and contacts that go on and slide or roll.
    [[nodiscard]] std::span<const Contact> Contacts() const;

    /// The outlines of the bodies whose bounds reach within radius of center, characters' bodies left out.
    [[nodiscard]] std::vector<BodyOutline> Outlines(const glm::vec3& center, float radius) const;

    /// The closest hit along a unit direction. Characters are hit through their CharacterBody, and the
    /// ignored body is not hit at all: a character looking or shooting ignores its own.
    [[nodiscard]] std::optional<RayHit> CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                                BodyId ignore = {}) const;

    /// Adds a character whose feet are at position.
    [[nodiscard]] CharacterHandle AddCharacter(const CharacterConfig& config, const glm::vec3& position);
    void RemoveCharacter(CharacterHandle character);

    /// Moves the character for one fixed step, sliding along what it hits, climbing steps and staying
    /// on the floor. Its vertical speed is the game's: gravity is applied by the caller.
    void MoveCharacter(CharacterHandle character, const glm::vec3& velocity, float step);
    void SetCharacterPosition(CharacterHandle character, const glm::vec3& position);
    /// The body that follows the character, which rays hit. It collides with nothing, so
    /// characters only push each other through their own test.
    [[nodiscard]] BodyId CharacterBody(CharacterHandle character) const;
    /// Where the feet are.
    [[nodiscard]] glm::vec3 CharacterPosition(CharacterHandle character) const;
    /// The velocity left after the last move: hitting a wall or a ceiling takes speed away.
    [[nodiscard]] glm::vec3 CharacterVelocity(CharacterHandle character) const;
    /// True when standing on walkable ground.
    [[nodiscard]] bool IsOnGround(CharacterHandle character) const;
    /// Changes the height from the feet up. Returns false and changes nothing if there is no room,
    /// for example when standing up under a pipe.
    bool SetCharacterHeight(CharacterHandle character, float height);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace daidalos::physics
