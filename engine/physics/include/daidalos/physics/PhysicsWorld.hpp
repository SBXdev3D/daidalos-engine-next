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

struct JointTag;
/// A joint between two bodies, or a body and the world (ADR-0126). The default value is no joint.
using JointHandle = core::Handle<JointTag>;

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
    /// Held where it is, as if hung on nothing, until ReleaseBody lets it fall (ADR-0127): crates waiting at the
    /// ceiling for their cue. Dynamic bodies push against it as against a wall.
    bool held = false;
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

/// How a joint holds two bodies together (ADR-0126).
enum class JointKind : std::uint8_t {
    /// A ball joint: the anchor stays shared, the bodies turn freely about it. Chains, a hanging lamp.
    Point,
    /// Turns about the axis through the anchor only: a door, a lid, a see-saw, a wheel.
    Hinge,
    /// Keeps the two anchors between a shortest and a longest distance: a rope, which goes slack.
    Distance,
    /// Holds the bodies as they are: glued, until it breaks.
    Fixed,
    /// Slides along the axis only, without turning: a drawer, a piston.
    Slider,
};

/// What drives a hinge or a slider (ADR-0126).
enum class MotorMode : std::uint8_t {
    Off,
    /// To a speed: radians per second for a hinge, meters per second for a slider.
    Speed,
    /// To a position: radians from where the hinge started, meters along the slider from where it started.
    Position,
};

struct JointMotor {
    MotorMode mode = MotorMode::Off;
    float target = 0.0f;
    /// The most it pushes: newton-meters for a hinge, newtons for a slider.
    float strength = 1000.0f;
    bool operator==(const JointMotor&) const = default;
};

struct JointDesc {
    JointKind kind = JointKind::Point;
    BodyId first;
    /// None: the joint holds the first body to the world.
    BodyId second;
    /// Where the joint is, in the world, as the bodies are now. A distance joint's first end.
    glm::vec3 anchor{0.0f};
    /// A distance joint's second end, in the world.
    glm::vec3 secondAnchor{0.0f};
    /// The hinge's or the slider's axis, in the world.
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    /// Limits: radians either side of where a hinge starts (within -pi to pi), meters along a slider from where it
    /// starts, or the shortest and longest distance of a distance joint. Without limits, a distance joint keeps
    /// the distance its anchors have now.
    bool limited = false;
    float lowest = 0.0f;
    float highest = 0.0f;
    /// What a hinge or a slider resists turning or sliding with: newton-meters or newtons. A heavy door swings
    /// less long.
    float friction = 0.0f;
    JointMotor motor;
    /// Newtons: past this pull, the joint breaks for good. 0 never breaks.
    float breakForce = 0.0f;
};

/// A joint that broke in the last Step, and where.
struct JointBreak {
    JointHandle joint;
    glm::vec3 position{0.0f};
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
    /// Lets a held body fall (ADR-0127). Nothing for others.
    void ReleaseBody(BodyId body);
    [[nodiscard]] bool IsHeld(BodyId body) const;
    /// What lies on the body is carried along at this velocity in the world, meters per second, as on a conveyor
    /// belt, while the body itself does not move (ADR-0127). Zero stops it.
    void SetSurfaceVelocity(BodyId body, const glm::vec3& velocity);
    /// Asleep: it has come to rest, and the simulation leaves it until something touches or pushes it.
    [[nodiscard]] bool IsSleeping(BodyId body) const;
    /// The dynamic bodies whose shapes reach into a sphere, in no set order.
    [[nodiscard]] std::vector<BodyId> DynamicBodiesInSphere(const glm::vec3& center, float radius) const;
    [[nodiscard]] bool IsDynamic(BodyId body) const;
    [[nodiscard]] float BodyMass(BodyId body) const;
    /// Removes its joints with it.
    void RemoveBody(BodyId body);
    [[nodiscard]] std::size_t BodyCount() const;

    /// The body reaches this position and rotation at the end of the next Step, which takes that long.
    void MoveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float step);
    [[nodiscard]] glm::vec3 BodyPosition(BodyId body) const;

    /// Moves the kinematic bodies by the fixed step, and the dynamic ones under gravity and what they touch.
    void Step(float step);
    /// What touched during the last Step (ADR-0125): new contacts, and contacts that go on and slide or roll.
    [[nodiscard]] std::span<const Contact> Contacts() const;

    /// Joins two bodies, or a body to the world (ADR-0126). Fails with InvalidArgument if the first body is not
    /// dynamic, if the second is the first, or for an axis of no length.
    [[nodiscard]] core::Result<JointHandle> AddJoint(const JointDesc& desc);
    void RemoveJoint(JointHandle joint);
    [[nodiscard]] std::size_t JointCount() const;
    /// Breaks it for good, as if pulled past its break force, waking what it held.
    void BreakJoint(JointHandle joint);
    [[nodiscard]] bool IsBroken(JointHandle joint) const;
    /// Changes what drives a hinge or a slider; nothing for other joints.
    void SetJointMotor(JointHandle joint, const JointMotor& motor);
    /// How far a hinge has turned (radians) or a slider has slid (meters) from where it started; 0 for others.
    [[nodiscard]] float JointPosition(JointHandle joint) const;
    /// How hard the joint pulled in the last Step, in newtons.
    [[nodiscard]] float JointForce(JointHandle joint) const;
    /// Where the joint is now, on its first body; and its other end, which a distance joint has apart.
    [[nodiscard]] glm::vec3 JointAnchor(JointHandle joint) const;
    [[nodiscard]] glm::vec3 JointSecondAnchor(JointHandle joint) const;
    /// The joints that broke in the last Step.
    [[nodiscard]] std::span<const JointBreak> BrokenJoints() const;

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
