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

// Joints (ADR-0126).

namespace {

BodyId AddWeight(PhysicsWorld& physics, const glm::vec3& at, float mass = 10.0f,
                 const glm::vec3& size = glm::vec3(0.4f)) {
    DynamicBodyDesc desc;
    desc.shape.size = size;
    desc.mass = mass;
    math::Transform start;
    start.translation = at;
    core::Result<BodyId> body = physics.AddDynamicBody(desc, start);
    EXPECT_TRUE(body.has_value());
    return body.value_or(BodyId{});
}

} // namespace

TEST(PhysicsWorldJoints, AJointNeedsADynamicFirstBodyAndTwoBodies) {
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    const BodyId weight = AddWeight(physics, {0.0f, 2.0f, 0.0f});
    EXPECT_FALSE(physics.AddJoint({.kind = JointKind::Point, .first = floor}).has_value());
    EXPECT_FALSE(physics.AddJoint({.kind = JointKind::Point, .first = weight, .second = weight}).has_value());
    EXPECT_FALSE(physics.AddJoint({.kind = JointKind::Hinge, .first = weight, .axis = glm::vec3(0.0f)}).has_value());
    EXPECT_EQ(physics.JointCount(), 0u);
}

TEST(PhysicsWorldJoints, AHangingWeightSwingsAndKeepsItsLength) {
    // A lamp 1.5 m under a ball joint on the ceiling, pushed sideways: it swings back past the middle, and stays
    // 1.5 m from the joint.
    PhysicsWorld physics;
    const glm::vec3 ceiling(0.0f, 5.0f, 0.0f);
    const BodyId lamp = AddWeight(physics, {0.0f, 3.5f, 0.0f});
    const core::Result<JointHandle> joint =
        physics.AddJoint({.kind = JointKind::Point, .first = lamp, .anchor = ceiling});
    ASSERT_TRUE(joint.has_value());
    physics.SetBodyVelocity(lamp, {3.0f, 0.0f, 0.0f});
    float farthest = 0.0f;
    float lowest = 0.0f;
    for (int step = 0; step < 180; ++step) {
        physics.Step(kStep);
        const glm::vec3 at = physics.BodyPosition(lamp);
        farthest = std::max(farthest, glm::distance(at, ceiling));
        lowest = std::min(lowest, at.x);
    }
    EXPECT_NEAR(farthest, 1.5f, 0.05f);
    EXPECT_LT(lowest, -0.5f) << "It swung back to the other side";
    EXPECT_NEAR(glm::distance(physics.JointAnchor(*joint), ceiling), 0.0f, 0.05f);
    EXPECT_GT(physics.JointForce(*joint), 50.0f) << "It holds about the lamp's weight, more as it swings";
}

TEST(PhysicsWorldJoints, ADoorStopsAtItsLimit) {
    // A door hinged on its left edge about Y, opening up to 90 degrees, pushed hard: it stops at 90.
    PhysicsWorld physics;
    const BodyId door = AddWeight(physics, {0.5f, 1.0f, 0.0f}, 20.0f, {1.0f, 2.0f, 0.05f});
    const core::Result<JointHandle> hinge = physics.AddJoint({.kind = JointKind::Hinge,
                                                              .first = door,
                                                              .anchor = {0.0f, 1.0f, 0.0f},
                                                              .axis = {0.0f, 1.0f, 0.0f},
                                                              .limited = true,
                                                              .lowest = 0.0f,
                                                              .highest = glm::radians(90.0f)});
    ASSERT_TRUE(hinge.has_value());
    // Gravity pulls along the axis: the door stays up.
    physics.AddImpulseAt(door, {0.0f, 0.0f, -60.0f}, {1.0f, 1.0f, 0.0f});
    float widest = 0.0f;
    for (int step = 0; step < 120; ++step) {
        physics.Step(kStep);
        widest = std::max(widest, std::abs(physics.JointPosition(*hinge)));
    }
    EXPECT_GT(widest, glm::radians(80.0f));
    EXPECT_LT(widest, glm::radians(95.0f));
    EXPECT_NEAR(physics.BodyPosition(door).y, 1.0f, 0.05f);
}

TEST(PhysicsWorldJoints, ARopeGoesSlackThenCatches) {
    // A rope of 2 m from a hook to a weight 1 m under it, thrown up: it rises and falls freely, then the rope
    // catches it 2 m under the hook.
    PhysicsWorld physics;
    const glm::vec3 hook(0.0f, 5.0f, 0.0f);
    const BodyId weight = AddWeight(physics, {0.0f, 4.0f, 0.0f});
    const core::Result<JointHandle> rope = physics.AddJoint({.kind = JointKind::Distance,
                                                             .first = weight,
                                                             .anchor = {0.0f, 4.0f, 0.0f},
                                                             .secondAnchor = hook,
                                                             .limited = true,
                                                             .lowest = 0.0f,
                                                             .highest = 2.0f});
    ASSERT_TRUE(rope.has_value());
    physics.Step(kStep);
    EXPECT_LT(physics.JointForce(*rope), 1.0f) << "Slack: it pulls nothing";
    for (int step = 0; step < 180; ++step) {
        physics.Step(kStep);
    }
    EXPECT_NEAR(physics.BodyPosition(weight).y, 3.0f, 0.1f);
    EXPECT_NEAR(physics.JointForce(*rope), 98.0f, 15.0f) << "Taut: it holds the weight";
}

TEST(PhysicsWorldJoints, AGlueBreaksUnderTooMuchWeightOnce) {
    // A 10 kg box glued under a static beam holds; a 100 kg one, past the glue's 500 N, breaks it and falls.
    PhysicsWorld physics;
    AddFloor(physics);
    const BodyId light = AddWeight(physics, {-2.0f, 3.0f, 0.0f}, 10.0f);
    const BodyId heavy = AddWeight(physics, {2.0f, 3.0f, 0.0f}, 100.0f);
    const core::Result<JointHandle> holds = physics.AddJoint(
        {.kind = JointKind::Fixed, .first = light, .anchor = {-2.0f, 3.2f, 0.0f}, .breakForce = 500.0f});
    const core::Result<JointHandle> breaks = physics.AddJoint(
        {.kind = JointKind::Fixed, .first = heavy, .anchor = {2.0f, 3.2f, 0.0f}, .breakForce = 500.0f});
    ASSERT_TRUE(holds.has_value() && breaks.has_value());
    int broken = 0;
    for (int step = 0; step < 120; ++step) {
        physics.Step(kStep);
        for (const JointBreak& snap : physics.BrokenJoints()) {
            ++broken;
            EXPECT_EQ(snap.joint, *breaks);
            EXPECT_GT(snap.position.y, 2.5f);
        }
    }
    EXPECT_EQ(broken, 1);
    EXPECT_FALSE(physics.IsBroken(*holds));
    EXPECT_TRUE(physics.IsBroken(*breaks));
    EXPECT_NEAR(physics.BodyPosition(light).y, 3.0f, 0.05f);
    EXPECT_NEAR(physics.BodyPosition(heavy).y, 0.2f, 0.05f);
}

TEST(PhysicsWorldJoints, ABrokenJointLetsGo) {
    PhysicsWorld physics;
    const BodyId weight = AddWeight(physics, {0.0f, 3.0f, 0.0f});
    const JointHandle joint =
        physics.AddJoint({.kind = JointKind::Fixed, .first = weight, .anchor = {0.0f, 3.0f, 0.0f}}).value();
    RunSteps(physics, 30);
    EXPECT_NEAR(physics.BodyPosition(weight).y, 3.0f, 0.02f);
    physics.BreakJoint(joint);
    RunSteps(physics, 30);
    EXPECT_LT(physics.BodyPosition(weight).y, 2.0f);
}

TEST(PhysicsWorldJoints, MotorsTurnAndSlide) {
    // A paddle wheel turning at 2 rad/s, and a piston driven 1 m along its slider.
    PhysicsWorld physics;
    const BodyId wheel = AddWeight(physics, {-3.0f, 3.0f, 0.0f}, 20.0f, {2.0f, 0.2f, 0.2f});
    const core::Result<JointHandle> axle = physics.AddJoint({.kind = JointKind::Hinge,
                                                             .first = wheel,
                                                             .anchor = {-3.0f, 3.0f, 0.0f},
                                                             .axis = {0.0f, 0.0f, 1.0f},
                                                             .motor = {.mode = MotorMode::Speed, .target = 2.0f}});
    const BodyId ram = AddWeight(physics, {3.0f, 3.0f, 0.0f}, 20.0f);
    const core::Result<JointHandle> piston =
        physics.AddJoint({.kind = JointKind::Slider,
                          .first = ram,
                          .anchor = {3.0f, 3.0f, 0.0f},
                          .axis = {1.0f, 0.0f, 0.0f},
                          .motor = {.mode = MotorMode::Position, .target = 1.0f, .strength = 5000.0f}});
    ASSERT_TRUE(axle.has_value() && piston.has_value());
    RunSteps(physics, 180);
    EXPECT_NEAR(std::abs(physics.BodyAngularVelocity(wheel).z), 2.0f, 0.1f);
    EXPECT_NEAR(physics.JointPosition(*piston), 1.0f, 0.05f);
    EXPECT_NEAR(physics.BodyPosition(ram).x, 4.0f, 0.05f);
    // Off, the wheel only coasts; the ram is driven back.
    physics.SetJointMotor(*axle, {});
    physics.SetJointMotor(*piston, {.mode = MotorMode::Position, .target = 0.0f, .strength = 5000.0f});
    RunSteps(physics, 180);
    EXPECT_NEAR(physics.BodyPosition(ram).x, 3.0f, 0.05f);
}

TEST(PhysicsWorldJoints, AChainHoldsALampWithoutStretching) {
    // Ten links of 10 cm and 0.3 kg each, ball-jointed, holding a 10 kg lamp. Light links under a heavy weight
    // stretch even with the joints' extra solver steps, so a chain also has a hidden rope as long as itself from
    // the hook to the lamp: it does not stretch more than a few centimeters.
    PhysicsWorld physics;
    const glm::vec3 top(0.0f, 6.0f, 0.0f);
    BodyId above;
    for (int link = 0; link < 10; ++link) {
        const glm::vec3 center = top - glm::vec3(0.0f, 0.05f + 0.1f * static_cast<float>(link), 0.0f);
        const BodyId body = AddWeight(physics, center, 0.3f, {0.03f, 0.1f, 0.03f});
        const glm::vec3 joint = center + glm::vec3(0.0f, 0.05f, 0.0f);
        ASSERT_TRUE(
            physics.AddJoint({.kind = JointKind::Point, .first = body, .second = above, .anchor = joint}).has_value());
        above = body;
    }
    const BodyId lamp = AddWeight(physics, top - glm::vec3(0.0f, 1.2f, 0.0f), 10.0f);
    ASSERT_TRUE(
        physics
            .AddJoint(
                {.kind = JointKind::Point, .first = lamp, .second = above, .anchor = top - glm::vec3(0.0f, 1.0f, 0.0f)})
            .has_value());
    ASSERT_TRUE(physics
                    .AddJoint({.kind = JointKind::Distance,
                               .first = lamp,
                               .anchor = physics.BodyPosition(lamp),
                               .secondAnchor = top,
                               .limited = true,
                               .lowest = 0.0f,
                               .highest = 1.2f})
                    .has_value());
    physics.SetBodyVelocity(lamp, {2.0f, 0.0f, 0.0f});
    float longest = 0.0f;
    for (int step = 0; step < 300; ++step) {
        physics.Step(kStep);
        longest = std::max(longest, glm::distance(physics.BodyPosition(lamp), top));
    }
    EXPECT_LT(longest, 1.2f * 1.05f);
}

TEST(PhysicsWorldJoints, RemovingABodyRemovesItsJoints) {
    PhysicsWorld physics;
    const BodyId first = AddWeight(physics, {0.0f, 3.0f, 0.0f});
    const BodyId second = AddWeight(physics, {0.0f, 2.0f, 0.0f});
    ASSERT_TRUE(physics.AddJoint({.kind = JointKind::Point, .first = first, .anchor = {0.0f, 3.5f, 0.0f}}));
    ASSERT_TRUE(
        physics.AddJoint({.kind = JointKind::Point, .first = second, .second = first, .anchor = {0.0f, 2.5f, 0.0f}}));
    EXPECT_EQ(physics.JointCount(), 2u);
    physics.RemoveBody(first);
    EXPECT_EQ(physics.JointCount(), 0u);
    RunSteps(physics, 10);
    EXPECT_LT(physics.BodyPosition(second).y, 2.0f) << "It falls now";
}
