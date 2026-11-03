// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/Entity.hpp>
#include <daidalos/ecs/World.hpp>

#include <gtest/gtest.h>

#include <format>
#include <unordered_set>

using namespace daidalos;
using ecs::Entity;
using ecs::World;

TEST(Entity, DefaultIsNull) {
    EXPECT_TRUE(Entity{}.IsNull());
    EXPECT_EQ(Entity{}, ecs::kNullEntity);
}

TEST(Entity, EqualityComparesIndexAndGeneration) {
    EXPECT_EQ(Entity(3, 1), Entity(3, 1));
    EXPECT_NE(Entity(3, 1), Entity(3, 2));
    EXPECT_NE(Entity(3, 1), Entity(4, 1));
}

TEST(Entity, FormatsAsIndexAndGeneration) {
    EXPECT_EQ(std::format("{}", Entity(7, 2)), "7:2");
    EXPECT_EQ(std::format("{}", ecs::kNullEntity), "null");
}

TEST(Entity, HashDistinguishesGenerations) {
    const std::unordered_set<Entity> set{Entity(1, 0), Entity(1, 1), Entity(2, 0)};
    EXPECT_EQ(set.size(), 3u);
    EXPECT_TRUE(set.contains(Entity(1, 1)));
}

TEST(EntityLifetime, CreatedEntitiesAreDistinctAndAlive) {
    World world;
    const Entity a = world.Create();
    const Entity b = world.Create();
    EXPECT_NE(a, b);
    EXPECT_FALSE(a.IsNull());
    EXPECT_TRUE(world.IsAlive(a));
    EXPECT_TRUE(world.IsAlive(b));
    EXPECT_EQ(world.AliveCount(), 2u);
}

TEST(EntityLifetime, NullEntityIsNeverAlive) {
    World world;
    EXPECT_FALSE(world.IsAlive(ecs::kNullEntity));
    EXPECT_FALSE(world.Destroy(ecs::kNullEntity));
}

TEST(EntityLifetime, DestroyedEntityIsNoLongerAlive) {
    World world;
    const Entity entity = world.Create();
    EXPECT_TRUE(world.Destroy(entity));
    EXPECT_FALSE(world.IsAlive(entity));
    EXPECT_EQ(world.AliveCount(), 0u);
}

TEST(EntityLifetime, DestroyingTwiceReturnsFalse) {
    World world;
    const Entity entity = world.Create();
    EXPECT_TRUE(world.Destroy(entity));
    EXPECT_FALSE(world.Destroy(entity));
}

TEST(EntityLifetime, ReusedSlotGetsNewGeneration) {
    World world;
    const Entity old = world.Create();
    world.Destroy(old);

    const Entity reused = world.Create();

    EXPECT_EQ(reused.Index(), old.Index());
    EXPECT_NE(reused.Generation(), old.Generation());
    EXPECT_TRUE(world.IsAlive(reused));
    EXPECT_FALSE(world.IsAlive(old));
}

TEST(EntityLifetime, StaleHandleCannotDestroyTheNewEntity) {
    World world;
    const Entity old = world.Create();
    world.Destroy(old);
    const Entity reused = world.Create();

    EXPECT_FALSE(world.Destroy(old));
    EXPECT_TRUE(world.IsAlive(reused));
}

TEST(EntityLifetime, HandleToFreeSlotIsNotAlive) {
    // A handle made up with the slot's next generation must not pass for a live entity.
    World world;
    const Entity entity = world.Create();
    world.Destroy(entity);
    EXPECT_FALSE(world.IsAlive(Entity(entity.Index(), entity.Generation() + 1)));
}

TEST(EntityLifetime, HandleFromOutOfRangeIndexIsNotAlive) {
    World world;
    EXPECT_FALSE(world.IsAlive(Entity(1000, 0)));
}

TEST(EntityLifetime, ManyEntitiesCanBeCreatedAndDestroyed) {
    World world;
    std::unordered_set<Entity> entities;
    for (int i = 0; i < 1000; ++i) {
        entities.insert(world.Create());
    }
    EXPECT_EQ(entities.size(), 1000u);
    for (const Entity entity : entities) {
        EXPECT_TRUE(world.Destroy(entity));
    }
    EXPECT_EQ(world.AliveCount(), 0u);
}
