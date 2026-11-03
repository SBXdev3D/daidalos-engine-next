// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/math/Transform.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>

#include <gtest/gtest.h>

#include <array>
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
void Walk(PhysicsWorld& physics, CharacterId character, const glm::vec3& horizontal, int steps) {
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
    const CharacterId character = physics.AddCharacter({}, {0.0f, 2.0f, 0.0f});
    EXPECT_FALSE(physics.IsOnGround(character));

    Walk(physics, character, glm::vec3(0.0f), 120);

    EXPECT_TRUE(physics.IsOnGround(character));
    EXPECT_NEAR(physics.CharacterPosition(character).y, 0.0f, 0.05f);
}

TEST(PhysicsWorld, CharacterWalksAtItsSpeed) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
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
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});

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
    const CharacterId character = physics.AddCharacter(config, {10.0f, 0.1f, 0.0f});
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
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
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
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
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

    const CharacterId climber = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    const CharacterId blocked = physics.AddCharacter({}, {0.0f, 0.1f, 6.0f});
    Walk(physics, climber, {3.0f, 0.0f, 0.0f}, 60);
    Walk(physics, blocked, {3.0f, 0.0f, 0.0f}, 60);

    EXPECT_NEAR(physics.CharacterPosition(climber).y, 0.3f, 0.1f);
    EXPECT_LT(physics.CharacterPosition(blocked).x, 2.0f);
}

TEST(PhysicsWorld, RayHitsACharacterThroughItsBody) {
    PhysicsWorld physics;
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    const std::optional<RayHit> hit = physics.CastRay({-5.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, physics.CharacterBody(character));
    EXPECT_NEAR(hit->position.x, -0.35f, 0.01f);
    EXPECT_EQ(physics.BodyCount(), 0U);
}

TEST(PhysicsWorld, RayIgnoresTheGivenBody) {
    PhysicsWorld physics;
    const BodyId floor = AddFloor(physics);
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    // From inside the character, down to the floor.
    const std::optional<RayHit> hit =
        physics.CastRay({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f, physics.CharacterBody(character));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, floor);
}

TEST(PhysicsWorld, CharacterBodyFollowsTheCharacter) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
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
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, character, glm::vec3(0.0f), 30);
    const auto headHit = [&] { return physics.CastRay({-5.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f).has_value(); };
    EXPECT_TRUE(headHit());
    ASSERT_TRUE(physics.SetCharacterHeight(character, 0.9f));
    EXPECT_FALSE(headHit());
}

TEST(PhysicsWorld, CharactersStillBlockEachOther) {
    PhysicsWorld physics;
    AddFloor(physics);
    const CharacterId walker = physics.AddCharacter({}, {-3.0f, 0.1f, 0.0f});
    const CharacterId standing = physics.AddCharacter({}, {0.0f, 0.1f, 0.0f});
    Walk(physics, walker, {3.0f, 0.0f, 0.0f}, 120);
    EXPECT_LT(physics.CharacterPosition(walker).x, physics.CharacterPosition(standing).x - 0.6f);
}

TEST(PhysicsWorld, RemovedCharacterIsNoLongerHit) {
    PhysicsWorld physics;
    const CharacterId character = physics.AddCharacter({}, {0.0f, 0.0f, 0.0f});
    physics.RemoveCharacter(character);
    EXPECT_FALSE(physics.CastRay({-5.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 10.0f).has_value());
}
