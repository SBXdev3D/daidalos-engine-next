// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/math/Axes.hpp>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <format>

using namespace daidalos;
using ecs::Entity;
using ecs::LocalTransform;
using ecs::World;
using ecs::WorldTransform;

namespace {

constexpr float kTolerance = 1e-4f;

Entity CreateAt(World& world, const glm::vec3& translation, Entity parent = ecs::kNullEntity) {
    const Entity entity = world.Create();
    LocalTransform& local = world.Add<LocalTransform>(entity);
    local.translation = translation;
    if (!parent.IsNull()) {
        EXPECT_TRUE(world.SetParent(entity, parent).has_value());
    }
    return entity;
}

glm::vec3 WorldPosition(const World& world, Entity entity) {
    return glm::vec3(world.Get<WorldTransform>(entity).matrix[3]);
}

testing::AssertionResult IsAt(const World& world, Entity entity, const glm::vec3& expected) {
    const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
    if (transform == nullptr) {
        return testing::AssertionFailure() << "No WorldTransform";
    }
    const glm::vec3 actual(transform->matrix[3]);
    if (glm::length(actual - expected) <= kTolerance) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << std::format("({}, {}, {}) is not ({}, {}, {})", actual.x, actual.y, actual.z,
                                                      expected.x, expected.y, expected.z);
}

} // namespace

TEST(Transforms, WorldTransformIsAddedAndEqualsLocalForRoots) {
    World world;
    const Entity entity = CreateAt(world, {1.0f, 2.0f, 3.0f});
    EXPECT_FALSE(world.Has<WorldTransform>(entity));

    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, entity, {1.0f, 2.0f, 3.0f}));
}

TEST(Transforms, ChildIsPlacedRelativeToItsParent) {
    World world;
    const Entity parent = CreateAt(world, {10.0f, 0.0f, 0.0f});
    world.Get<LocalTransform>(parent).rotation = glm::angleAxis(glm::radians(90.0f), math::kUp);
    const Entity child = CreateAt(world, math::kForward, parent);

    ecs::PropagateTransforms(world);

    // The parent faces -X after turning left, so one meter ahead of it is one meter towards -X.
    EXPECT_TRUE(IsAt(world, child, {9.0f, 0.0f, 0.0f}));
}

TEST(Transforms, ParentScaleAppliesToChildOffset) {
    World world;
    const Entity parent = CreateAt(world, glm::vec3(0.0f));
    world.Get<LocalTransform>(parent).scale = glm::vec3(3.0f);
    const Entity child = CreateAt(world, {1.0f, 0.0f, 0.0f}, parent);

    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, child, {3.0f, 0.0f, 0.0f}));
}

TEST(Transforms, ChainsAccumulateThroughEveryLevel) {
    World world;
    const Entity root = CreateAt(world, {1.0f, 0.0f, 0.0f});
    const Entity middle = CreateAt(world, {0.0f, 2.0f, 0.0f}, root);
    const Entity leaf = CreateAt(world, {0.0f, 0.0f, 3.0f}, middle);

    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, leaf, {1.0f, 2.0f, 3.0f}));
}

TEST(Transforms, ChildCreatedBeforeItsParentIsStillCorrect) {
    // Storage order must not matter: the child is first in the pool.
    World world;
    const Entity child = CreateAt(world, {0.0f, 1.0f, 0.0f});
    const Entity parent = CreateAt(world, {5.0f, 0.0f, 0.0f});
    ASSERT_TRUE(world.SetParent(child, parent).has_value());

    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, child, {5.0f, 1.0f, 0.0f}));
}

TEST(Transforms, MovingAnAncestorMovesDescendantsAtNextPropagation) {
    World world;
    const Entity root = CreateAt(world, glm::vec3(0.0f));
    const Entity middle = CreateAt(world, {0.0f, 1.0f, 0.0f}, root);
    const Entity leaf = CreateAt(world, {0.0f, 1.0f, 0.0f}, middle);
    ecs::PropagateTransforms(world);

    world.Get<LocalTransform>(root).translation = {4.0f, 0.0f, 0.0f};
    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, leaf, {4.0f, 2.0f, 0.0f}));
}

TEST(Transforms, ReparentedChildFollowsItsNewParent) {
    World world;
    const Entity first = CreateAt(world, {1.0f, 0.0f, 0.0f});
    const Entity second = CreateAt(world, {-1.0f, 0.0f, 0.0f});
    const Entity child = CreateAt(world, {0.0f, 0.0f, 1.0f}, first);
    ecs::PropagateTransforms(world);

    ASSERT_TRUE(world.SetParent(child, second).has_value());
    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, child, {-1.0f, 0.0f, 1.0f}));
}

TEST(Transforms, DetachedChildIsPlacedRelativeToTheWorld) {
    World world;
    const Entity parent = CreateAt(world, {10.0f, 0.0f, 0.0f});
    const Entity child = CreateAt(world, {1.0f, 0.0f, 0.0f}, parent);
    ecs::PropagateTransforms(world);

    ASSERT_TRUE(world.SetParent(child, ecs::kNullEntity).has_value());
    ecs::PropagateTransforms(world);

    EXPECT_TRUE(IsAt(world, child, {1.0f, 0.0f, 0.0f}));
}

TEST(Transforms, ParentWithoutLocalTransformDoesNotMoveItsChild) {
    World world;
    const Entity group = world.Create();
    const Entity child = CreateAt(world, {1.0f, 0.0f, 0.0f}, group);
    const Entity grandchild = CreateAt(world, {0.0f, 1.0f, 0.0f}, child);

    ecs::PropagateTransforms(world);

    EXPECT_FALSE(world.Has<WorldTransform>(group));
    EXPECT_TRUE(IsAt(world, child, {1.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(IsAt(world, grandchild, {1.0f, 1.0f, 0.0f}));
}

TEST(Transforms, ChildrenOfDestroyedParentAreGone) {
    World world;
    const Entity parent = CreateAt(world, glm::vec3(0.0f));
    CreateAt(world, glm::vec3(1.0f), parent);
    const Entity other = CreateAt(world, {2.0f, 0.0f, 0.0f});
    ecs::PropagateTransforms(world);

    world.Destroy(parent);
    ecs::PropagateTransforms(world);

    EXPECT_EQ(world.Count<WorldTransform>(), 1u);
    EXPECT_TRUE(IsAt(world, other, {2.0f, 0.0f, 0.0f}));
}

TEST(Transforms, DeepHierarchyDoesNotOverflowTheStack) {
    World world;
    Entity parent = CreateAt(world, glm::vec3(0.0f));
    for (int depth = 0; depth < 10000; ++depth) {
        parent = CreateAt(world, {0.0f, 0.001f, 0.0f}, parent);
    }

    ecs::PropagateTransforms(world);

    EXPECT_NEAR(WorldPosition(world, parent).y, 10.0f, 1e-2f);
}
