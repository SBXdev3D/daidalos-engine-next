// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/math/Transform.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace daidalos;
using namespace daidalos::physics;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kGravity = 9.8f;

/// A floor of 40 by 40 meters at y = 0, facing up.
BodyId AddFloor(PhysicsWorld& physics) {
    const std::array<glm::vec3, 4> positions{glm::vec3(-20.0f, 0.0f, -20.0f), glm::vec3(-20.0f, 0.0f, 20.0f),
                                             glm::vec3(20.0f, 0.0f, 20.0f), glm::vec3(20.0f, 0.0f, -20.0f)};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    core::Result<BodyId> floor = physics.AddStaticMesh(positions, indices);
    EXPECT_TRUE(floor.has_value());
    return floor.value_or(BodyId{});
}

/// Moves the character for a number of steps, under gravity.
void Walk(PhysicsWorld& physics, CharacterHandle character, const glm::vec3& horizontal, int steps) {
    for (int step = 0; step < steps; ++step) {
        const float fall =
            physics.IsOnGround(character) ? 0.0f : physics.CharacterVelocity(character).y - kGravity * kStep;
        physics.Step(kStep);
        physics.MoveCharacter(character, horizontal + glm::vec3(0.0f, fall, 0.0f), kStep);
    }
}

} // namespace

TEST(PhysicsWorld, WorldsCanComeAndGo) {
    for (int world = 0; world < 3; ++world) {
        PhysicsWorld first;
        const PhysicsWorld second;
        AddFloor(first);
        EXPECT_EQ(first.BodyCount(), 1u);
        EXPECT_EQ(second.BodyCount(), 0u);
    }
}

TEST(PhysicsWorld, MeshWithoutTrianglesIsAnError) {
    PhysicsWorld physics;
    const std::vector<glm::vec3> positions;
    const std::vector<std::uint32_t> indices;
    core::Result<BodyId> body = physics.AddStaticMesh(positions, indices);
    ASSERT_FALSE(body.has_value());
    EXPECT_EQ(body.error().code, core::ErrorCode::InvalidArgument);
}

TEST(PhysicsWorld, RayHitsTheClosestSurface) {
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    physics.Step(kStep);

    const std::optional<RayHit> hit = physics.CastRay({1.0f, 2.0f, 3.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.001f);
    EXPECT_NEAR(hit->position.y, 0.0f, 0.001f);
    EXPECT_NEAR(hit->normal.y, 1.0f, 0.001f);
    EXPECT_EQ(hit->body, floor);
}

TEST(PhysicsWorld, RayStopsAtItsLength) {
    PhysicsWorld physics;
    AddFloor(physics);
    physics.Step(kStep);
    EXPECT_FALSE(physics.CastRay({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 4.0f).has_value());
}

TEST(PhysicsWorld, RemovedBodyIsNoLongerHit) {
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    physics.Step(kStep);
    physics.RemoveBody(floor);
    physics.Step(kStep);
    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_FALSE(physics.CastRay({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f).has_value());
}

TEST(PhysicsWorld, CharacterFallsAndStandsOnTheFloor) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 2.0f, 0.0f});
    EXPECT_FALSE(physics.IsOnGround(character));

    Walk(physics, character, glm::vec3(0.0f), 120);

    EXPECT_TRUE(physics.IsOnGround(character));
    EXPECT_NEAR(physics.CharacterPosition(character).y, 0.0f, 0.05f);
}

TEST(PhysicsWorld, CharacterWalksAtItsSpeed) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    const float start = physics.CharacterPosition(character).x;

    Walk(physics, character, {3.0f, 0.0f, 0.0f}, 60);

    EXPECT_NEAR(physics.CharacterPosition(character).x - start, 3.0f, 0.1f);
}

TEST(PhysicsWorld, KinematicBoxBlocksTheCharacterUntilItMoves) {
    PhysicsWorld physics;
    AddFloor(physics);
    math::Transform door;
    door.translation = {2.0f, 1.0f, 0.0f};
    core::Result<BodyId> box = physics.AddKinematicBox({0.1f, 1.0f, 5.0f}, {}, door);
    ASSERT_TRUE(box.has_value());
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});

    Walk(physics, character, {3.0f, 0.0f, 0.0f}, 120);
    EXPECT_LT(physics.CharacterPosition(character).x, 1.6f);

    // Up and out of the way, slowly, as a door does.
    for (int step = 0; step < 90; ++step) {
        door.translation.y += 0.05f;
        physics.MoveKinematic(*box, door.translation, door.rotation, kStep);
        Walk(physics, character, glm::vec3(0.0f), 1);
    }
    EXPECT_NEAR(physics.BodyPosition(*box).y, door.translation.y, 0.001f);
    Walk(physics, character, {3.0f, 0.0f, 0.0f}, 120);
    EXPECT_GT(physics.CharacterPosition(character).x, 3.0f);
}

TEST(PhysicsWorld, CharacterCannotStandUpUnderACeiling) {
    PhysicsWorld physics;
    AddFloor(physics);
    const std::array<glm::vec3, 4> positions{glm::vec3(-5.0f, 1.2f, -5.0f), glm::vec3(5.0f, 1.2f, -5.0f),
                                             glm::vec3(5.0f, 1.2f, 5.0f), glm::vec3(-5.0f, 1.2f, 5.0f)};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    ASSERT_TRUE(physics.AddStaticMesh(positions, indices).has_value());
    physics.Step(kStep);

    CharacterConfig config;
    const CharacterHandle character = physics.AddCharacter(config, {10.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    EXPECT_TRUE(physics.SetCharacterHeight(character, 0.9f));
    Walk(physics, character, {-3.0f, 0.0f, 0.0f}, 120);
    ASSERT_LT(physics.CharacterPosition(character).x, 4.5f);

    EXPECT_FALSE(physics.SetCharacterHeight(character, config.height));

    Walk(physics, character, {3.0f, 0.0f, 0.0f}, 180);
    EXPECT_TRUE(physics.SetCharacterHeight(character, config.height));
}

TEST(PhysicsWorld, CeilingStopsAJump) {
    PhysicsWorld physics;
    AddFloor(physics);
    const std::array<glm::vec3, 4> positions{glm::vec3(-5.0f, 2.2f, -5.0f), glm::vec3(5.0f, 2.2f, -5.0f),
                                             glm::vec3(5.0f, 2.2f, 5.0f), glm::vec3(-5.0f, 2.2f, 5.0f)};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    ASSERT_TRUE(physics.AddStaticMesh(positions, indices).has_value());
    physics.Step(kStep);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    ASSERT_TRUE(physics.IsOnGround(character));

    for (int step = 0; step < 30; ++step) {
        physics.MoveCharacter(character, {0.0f, 5.0f, 0.0f}, kStep);
    }

    EXPECT_NEAR(physics.CharacterPosition(character).y, 2.2f - 1.85f, 0.05f);
    EXPECT_LT(physics.CharacterVelocity(character).y, 1.0f);
}

TEST(PhysicsWorld, CharacterLeavesTheGroundWhenJumping) {
    PhysicsWorld physics;
    AddFloor(physics);
    physics.Step(kStep);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    ASSERT_TRUE(physics.IsOnGround(character));

    physics.MoveCharacter(character, {0.0f, 3.5f, 0.0f}, kStep);
    physics.MoveCharacter(character, {0.0f, 3.4f, 0.0f}, kStep);

    EXPECT_GT(physics.CharacterPosition(character).y, 0.08f);
    EXPECT_FALSE(physics.IsOnGround(character));
}

TEST(PhysicsWorld, CharacterClimbsASmallStepButNotAWall) {
    PhysicsWorld physics;
    AddFloor(physics);
    const auto box = [&physics](float height, float z) {
        // Top and a face toward the character, walking along +X.
        const std::array<glm::vec3, 8> positions{glm::vec3(2.0f, height, z - 1.0f), glm::vec3(2.0f, height, z + 1.0f),
                                                 glm::vec3(4.0f, height, z + 1.0f), glm::vec3(4.0f, height, z - 1.0f),
                                                 glm::vec3(2.0f, 0.0f, z - 1.0f),   glm::vec3(2.0f, 0.0f, z + 1.0f),
                                                 glm::vec3(2.0f, height, z + 1.0f), glm::vec3(2.0f, height, z - 1.0f)};
        const std::array<std::uint32_t, 12> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        EXPECT_TRUE(physics.AddStaticMesh(positions, indices).has_value());
    };
    box(0.3f, 0.0f);
    box(1.5f, 6.0f);
    physics.Step(kStep);

    const CharacterHandle climber = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    const CharacterHandle blocked = physics.AddCharacter({}, {0.0f, 0.1f, 6.0f});
    Walk(physics, climber, {3.0f, 0.0f, 0.0f}, 60);
    Walk(physics, blocked, {3.0f, 0.0f, 0.0f}, 60);

    EXPECT_NEAR(physics.CharacterPosition(climber).y, 0.3f, 0.1f);
    EXPECT_LT(physics.CharacterPosition(blocked).x, 2.0f);
}

TEST(PhysicsWorld, RayHitsACharacterThroughItsBody) {
    PhysicsWorld physics;
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    const std::optional<RayHit> hit = physics.CastRay({-5.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, physics.CharacterBody(character));
    EXPECT_NEAR(hit->position.x, -0.35f, 0.01f);
    EXPECT_EQ(physics.BodyCount(), 0U);
}

TEST(PhysicsWorld, RayIgnoresTheGivenBody) {
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    // From inside the character, down to the floor.
    const std::optional<RayHit> hit =
        physics.CastRay({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f, physics.CharacterBody(character));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, floor);
}

TEST(PhysicsWorld, CharacterBodyFollowsTheCharacter) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, {3.0f, 0.0f, 0.0f}, 60);
    const glm::vec3 feet = physics.CharacterPosition(character);

    const std::optional<RayHit> there = physics.CastRay(feet + glm::vec3(0.0f, 1.0f, -5.0f), {0.0f, 0.0f, 1.0f}, 10.0f);
    ASSERT_TRUE(there.has_value());
    EXPECT_EQ(there->body, physics.CharacterBody(character));
    EXPECT_FALSE(physics.CastRay({0.0f, 1.0f, -5.0f}, {0.0f, 0.0f, 1.0f}, 10.0f).has_value());
}

TEST(PhysicsWorld, CrouchingLowersTheBody) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    const auto headHit = [&] { return physics.CastRay({-5.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f).has_value(); };
    EXPECT_TRUE(headHit());
    ASSERT_TRUE(physics.SetCharacterHeight(character, 0.9f));
    EXPECT_FALSE(headHit());
}

TEST(PhysicsWorld, CharactersStillBlockEachOther) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterHandle walker = physics.AddCharacter({}, {-3.0f, 0.1f, 0.0f});
    const CharacterHandle standing = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, walker, {3.0f, 0.0f, 0.0f}, 120);
    EXPECT_LT(physics.CharacterPosition(walker).x, physics.CharacterPosition(standing).x - 0.6f);
}

TEST(PhysicsWorld, RemovedCharacterIsNoLongerHit) {
    PhysicsWorld physics;
    const CharacterHandle character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    physics.RemoveCharacter(character);
    EXPECT_FALSE(physics.CastRay({-5.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f).has_value());
}

TEST(PhysicsWorld, ABoxFallsAndComesToRestOnTheFloor) {
    // A loose crate (ADR-0073): it falls, lands and stays.
    PhysicsWorld physics;
    AddFloor(physics);
    math::Transform start;
    start.translation = {0.0f, 3.0f, 0.0f};
    const core::Result<BodyId> crate = physics.AddDynamicBox(glm::vec3(0.3f), 15.0f, start);
    ASSERT_TRUE(crate.has_value());
    EXPECT_TRUE(physics.IsDynamic(*crate));
    EXPECT_NEAR(physics.BodyMass(*crate), 15.0f, 0.01f);
    for (int step = 0; step < 240; ++step) {
        physics.Step(1.0f / 60.0f);
    }
    EXPECT_NEAR(physics.BodyPosition(*crate).y, 0.3f, 0.05f);
    EXPECT_LT(glm::length(physics.BodyVelocity(*crate)), 0.05f);
}

TEST(PhysicsWorld, AThrownBoxFliesTheWayItWasThrown) {
    PhysicsWorld physics;
    AddFloor(physics);
    math::Transform start;
    start.translation = {0.0f, 0.3f, 0.0f};
    const core::Result<BodyId> crate = physics.AddDynamicBox(glm::vec3(0.3f), 10.0f, start);
    ASSERT_TRUE(crate.has_value());
    physics.Step(1.0f / 60.0f);
    physics.SetBodyVelocity(*crate, {0.0f, 3.0f, -6.0f});
    for (int step = 0; step < 30; ++step) {
        physics.Step(1.0f / 60.0f);
    }
    EXPECT_LT(physics.BodyPosition(*crate).z, -2.0f);
    physics.SetBodyTransform(*crate, {5.0f, 2.0f, 5.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(physics.BodyPosition(*crate).x, 5.0f, 1e-4f);
}

namespace {

/// A slope of 40 by 40 meters through the origin, tilted about X by the angle (its +Z side goes down), with its
/// own friction.
BodyId AddSlope(PhysicsWorld& physics, float degrees, float friction = 0.6f) {
    const std::array<glm::vec3, 4> positions{glm::vec3(-20.0f, 0.0f, -20.0f), glm::vec3(-20.0f, 0.0f, 20.0f),
                                             glm::vec3(20.0f, 0.0f, 20.0f), glm::vec3(20.0f, 0.0f, -20.0f)};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    math::Transform tilt;
    tilt.rotation = glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f));
    core::Result<BodyId> slope = physics.AddStaticMesh(positions, indices, tilt, {friction, 0.0f, 0.0f, 0.0f});
    EXPECT_TRUE(slope.has_value());
    return slope.value_or(BodyId{});
}

BodyId AddBody(PhysicsWorld& physics, ShapeKind kind, const glm::vec3& size, const glm::vec3& at,
               const BodyMaterial& material = {}, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
    DynamicBodyDesc desc;
    desc.shape.kind = kind;
    desc.shape.size = size;
    desc.mass = 10.0f;
    desc.material = material;
    math::Transform start;
    start.translation = at;
    start.rotation = rotation;
    core::Result<BodyId> body = physics.AddDynamicBody(desc, start);
    EXPECT_TRUE(body.has_value());
    return body.value_or(BodyId{});
}

void RunSteps(PhysicsWorld& physics, int steps) {
    for (int step = 0; step < steps; ++step) {
        physics.Step(kStep);
    }
}

} // namespace

TEST(PhysicsWorld, EachShapeFallsAndRestsOnTheFloor) {
    // ADR-0125: a sphere, a standing cylinder and capsule, and a hull rest where their bottoms touch the floor.
    PhysicsWorld physics;
    AddFloor(physics);
    const BodyId sphere = AddBody(physics, ShapeKind::Sphere, glm::vec3(0.4f), {-3.0f, 2.0f, 0.0f});
    const BodyId cylinder = AddBody(physics, ShapeKind::Cylinder, glm::vec3(0.6f, 0.9f, 0.6f), {-1.0f, 2.0f, 0.0f});
    const BodyId capsule = AddBody(physics, ShapeKind::Capsule, glm::vec3(0.4f, 1.0f, 0.4f), {1.0f, 2.0f, 0.0f});
    DynamicBodyDesc hull;
    hull.shape.kind = ShapeKind::Hull;
    // A pyramid on a square base 1 m across, its base at y = 0 in its own frame.
    hull.shape.points = {
        {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}, {-0.5f, 0.0f, 0.5f}, {0.0f, 0.8f, 0.0f}};
    math::Transform start;
    start.translation = {3.0f, 2.0f, 0.0f};
    const core::Result<BodyId> pyramid = physics.AddDynamicBody(hull, start);
    ASSERT_TRUE(pyramid.has_value());
    RunSteps(physics, 300);
    EXPECT_NEAR(physics.BodyPosition(sphere).y, 0.2f, 0.03f);
    EXPECT_NEAR(physics.BodyPosition(cylinder).y, 0.45f, 0.03f);
    EXPECT_NEAR(physics.BodyPosition(capsule).y, 0.5f, 0.03f);
    EXPECT_NEAR(physics.BodyPosition(*pyramid).y, 0.0f, 0.03f);
}

TEST(PhysicsWorld, AHullOfTooFewPointsIsAnError) {
    PhysicsWorld physics;
    DynamicBodyDesc flat;
    flat.shape.kind = ShapeKind::Hull;
    flat.shape.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    EXPECT_FALSE(physics.AddDynamicBody(flat, {}).has_value());
}

TEST(PhysicsWorld, AHullOfManyPointsKeepsItsSize) {
    // Thousands of points on a sphere of 0.5 m make a hull Jolt builds from far fewer, about as big.
    PhysicsWorld physics;
    AddFloor(physics);
    DynamicBodyDesc ball;
    ball.shape.kind = ShapeKind::Hull;
    for (int ring = 0; ring <= 40; ++ring) {
        const float polar = glm::pi<float>() * static_cast<float>(ring) / 40.0f;
        for (int around = 0; around < 80; ++around) {
            const float azimuth = glm::two_pi<float>() * static_cast<float>(around) / 80.0f;
            ball.shape.points.push_back(0.5f * glm::vec3(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                                                         std::sin(polar) * std::sin(azimuth)));
        }
    }
    math::Transform start;
    start.translation = {0.0f, 2.0f, 0.0f};
    const core::Result<BodyId> body = physics.AddDynamicBody(ball, start);
    ASSERT_TRUE(body.has_value());
    RunSteps(physics, 240);
    EXPECT_NEAR(physics.BodyPosition(*body).y, 0.5f, 0.08f);
}

TEST(PhysicsWorld, ACylinderRollsDownASlopeWhereABoxSlides) {
    // On its side across a 20 degree slope, a drum rolls: it turns fast. A box of the same size slides, or
    // stays, without turning.
    PhysicsWorld physics;
    AddSlope(physics, 20.0f);
    const glm::quat onItsSide = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const BodyId drum =
        AddBody(physics, ShapeKind::Cylinder, glm::vec3(0.6f, 0.9f, 0.6f), {-2.0f, 0.5f, 0.0f}, {}, onItsSide);
    const BodyId box = AddBody(physics, ShapeKind::Box, glm::vec3(0.6f, 0.6f, 0.9f), {2.0f, 0.5f, 0.0f});
    RunSteps(physics, 90);
    EXPECT_GT(glm::length(physics.BodyAngularVelocity(drum)), 2.0f);
    EXPECT_GT(physics.BodyPosition(drum).z, 1.0f); // Down the slope, towards +Z.
    EXPECT_LT(glm::length(physics.BodyAngularVelocity(box)), 0.5f);
}

TEST(PhysicsWorld, IceSlidesWhereWoodHolds) {
    PhysicsWorld physics;
    AddSlope(physics, 15.0f, 0.6f);
    const BodyId wood = AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {-2.0f, 0.4f, 0.0f}, {0.6f, 0.0f});
    const BodyId ice = AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {2.0f, 0.4f, 0.0f}, {0.03f, 0.0f});
    RunSteps(physics, 90);
    EXPECT_LT(physics.BodyPosition(wood).z, 0.3f);
    EXPECT_GT(physics.BodyPosition(ice).z, 1.0f);
}

TEST(PhysicsWorld, RubberBouncesWhereWoodLandsDead) {
    PhysicsWorld physics;
    AddFloor(physics);
    const BodyId rubber = AddBody(physics, ShapeKind::Sphere, glm::vec3(0.3f), {-1.0f, 2.0f, 0.0f}, {0.9f, 0.8f});
    const BodyId wood = AddBody(physics, ShapeKind::Sphere, glm::vec3(0.3f), {1.0f, 2.0f, 0.0f}, {0.6f, 0.0f});
    // The highest each gets after its first landing.
    float rubberHigh = 0.0f;
    float woodHigh = 0.0f;
    bool rubberLanded = false;
    bool woodLanded = false;
    for (int step = 0; step < 120; ++step) {
        physics.Step(kStep);
        rubberLanded = rubberLanded || physics.BodyVelocity(rubber).y > 0.5f;
        woodLanded = woodLanded || physics.BodyPosition(wood).y < 0.2f;
        if (rubberLanded) {
            rubberHigh = std::max(rubberHigh, physics.BodyPosition(rubber).y);
        }
        if (woodLanded) {
            woodHigh = std::max(woodHigh, physics.BodyPosition(wood).y);
        }
    }
    EXPECT_GT(rubberHigh, 0.8f);
    EXPECT_LT(woodHigh, 0.25f);
}

TEST(PhysicsWorld, AnImpulseOffCenterTurnsTheBody) {
    PhysicsWorld physics;
    const BodyId centered = AddBody(physics, ShapeKind::Box, glm::vec3(1.0f), {-3.0f, 5.0f, 0.0f});
    const BodyId offCenter = AddBody(physics, ShapeKind::Box, glm::vec3(1.0f), {3.0f, 5.0f, 0.0f});
    physics.AddImpulseAt(centered, {0.0f, 0.0f, -20.0f}, {-3.0f, 5.0f, 0.0f});
    physics.AddImpulseAt(offCenter, {0.0f, 0.0f, -20.0f}, {3.0f, 5.5f, 0.0f});
    physics.Step(kStep);
    EXPECT_LT(glm::length(physics.BodyAngularVelocity(centered)), 1e-3f);
    EXPECT_GT(glm::length(physics.BodyAngularVelocity(offCenter)), 1.0f);
    // Both move off at the same speed: impulse over mass.
    EXPECT_NEAR(physics.BodyVelocity(centered).z, -2.0f, 0.05f);
    EXPECT_NEAR(physics.BodyVelocity(offCenter).z, -2.0f, 0.05f);
}

TEST(PhysicsWorld, ALandingReportsHowFastItHit) {
    // From 1.25 m, the bottom of a 0.5 m box falls 1 m: it meets the floor at about 4.4 m/s.
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    const BodyId box = AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {0.0f, 1.25f, 0.0f});
    std::optional<Contact> landing;
    for (int step = 0; step < 90 && !landing; ++step) {
        physics.Step(kStep);
        for (const Contact& contact : physics.Contacts()) {
            if (contact.fresh) {
                landing = contact;
            }
        }
    }
    ASSERT_TRUE(landing.has_value());
    EXPECT_TRUE((landing->first == box && landing->second == floor) ||
                (landing->first == floor && landing->second == box));
    EXPECT_NEAR(landing->approach, 4.4f, 0.4f);
    EXPECT_NEAR(landing->point.y, 0.0f, 0.05f);
    // Once at rest, nothing more is reported.
    RunSteps(physics, 120);
    EXPECT_TRUE(physics.Contacts().empty());
    EXPECT_TRUE(physics.IsSleeping(box) || glm::length(physics.BodyVelocity(box)) < 0.05f);
}

TEST(PhysicsWorld, ARollingBodyBarelySlides) {
    // A ball pushed along the floor soon rolls: it touches at a point that does not slide.
    PhysicsWorld physics;
    AddFloor(physics);
    const BodyId ball = AddBody(physics, ShapeKind::Sphere, glm::vec3(0.4f), {0.0f, 0.2f, 0.0f});
    RunSteps(physics, 10);
    physics.SetBodyVelocity(ball, {3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -15.0f});
    RunSteps(physics, 30);
    bool touching = false;
    for (const Contact& contact : physics.Contacts()) {
        if (contact.first == ball || contact.second == ball) {
            touching = true;
            EXPECT_FALSE(contact.fresh);
            EXPECT_LT(contact.slide, 0.3f);
        }
    }
    EXPECT_TRUE(touching);
    EXPECT_GT(glm::length(physics.BodyVelocity(ball)), 1.0f);
}

TEST(PhysicsWorld, OnlyDynamicBodiesInTheSphereAreFound) {
    PhysicsWorld physics;
    AddFloor(physics);
    const BodyId near = AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {1.0f, 0.25f, 0.0f});
    AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {6.0f, 0.25f, 0.0f});
    const std::vector<BodyId> found = physics.DynamicBodiesInSphere({0.0f, 0.5f, 0.0f}, 2.0f);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found.front(), near);
    EXPECT_TRUE(physics.DynamicBodiesInSphere({6.0f, 0.5f, 0.0f}, 0.0f).empty());
}

TEST(PhysicsWorld, OutlinesDrawEachBodyButTheCharacters) {
    PhysicsWorld physics;
    AddFloor(physics); // Two triangles: few enough to be outlined.
    const BodyId box = AddBody(physics, ShapeKind::Box, glm::vec3(0.5f), {0.0f, 3.0f, 0.0f});
    const CharacterHandle character = physics.AddCharacter({}, {2.0f, 0.0f, 0.0f});
    const std::vector<BodyOutline> outlines = physics.Outlines({0.0f, 1.0f, 0.0f}, 10.0f);
    ASSERT_EQ(outlines.size(), 2u);
    for (const BodyOutline& outline : outlines) {
        EXPECT_NE(outline.body, physics.CharacterBody(character));
        EXPECT_FALSE(outline.lines.empty());
        EXPECT_EQ(outline.lines.size() % 2, 0u);
        if (outline.body == box) {
            EXPECT_TRUE(outline.dynamic);
            // Every point is on the box: within half its diagonal of its center.
            for (const glm::vec3& point : outline.lines) {
                EXPECT_LT(glm::distance(point, glm::vec3(0.0f, 3.0f, 0.0f)), 0.44f);
            }
        }
    }
}

TEST(PhysicsWorld, TheSameStartEndsTheSame) {
    // Recordings depend on it (ADR-0123, ADR-0125): two worlds from the same state end in the same place.
    std::array<glm::vec3, 2> ends{};
    for (glm::vec3& end : ends) {
        PhysicsWorld physics;
        AddFloor(physics);
        std::vector<BodyId> boxes;
        for (int index = 0; index < 10; ++index) {
            boxes.push_back(
                AddBody(physics, ShapeKind::Box, glm::vec3(0.5f),
                        {0.1f * static_cast<float>(index), 0.3f + 0.55f * static_cast<float>(index), 0.0f}));
        }
        physics.AddImpulseAt(boxes.front(), {30.0f, 0.0f, 10.0f}, {0.0f, 0.4f, 0.1f});
        RunSteps(physics, 240);
        end = physics.BodyPosition(boxes.back());
    }
    EXPECT_EQ(ends[0], ends[1]);
}
