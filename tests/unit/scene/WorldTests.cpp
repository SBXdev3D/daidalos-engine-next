// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/World.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AssertCapture.hpp"

using namespace daidalos;
using ecs::Entity;
using ecs::World;

namespace {

// Test components: an aggregate, a class with a constructor, and a move-only type.
struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float x = 0.0f;
    float y = 0.0f;
};

class Name {
public:
    explicit Name(std::string value) : m_value(std::move(value)) {}
    [[nodiscard]] const std::string& Value() const { return m_value; }

private:
    std::string m_value;
};

struct Owned {
    std::unique_ptr<int> value;
};

} // namespace

TEST(Components, AddedComponentCanBeRead) {
    World world;
    const Entity entity = world.Create();

    Position& added = world.Add<Position>(entity, 1.0f, 2.0f);

    EXPECT_EQ(added.x, 1.0f);
    EXPECT_TRUE(world.Has<Position>(entity));
    EXPECT_EQ(world.Get<Position>(entity).y, 2.0f);
    ASSERT_NE(world.TryGet<Position>(entity), nullptr);
    EXPECT_EQ(world.TryGet<Position>(entity)->x, 1.0f);
}

TEST(Components, ConstructorArgumentsAreForwarded) {
    World world;
    const Entity entity = world.Create();
    world.Add<Name>(entity, "door");
    EXPECT_EQ(world.Get<Name>(entity).Value(), "door");
}

TEST(Components, MoveOnlyComponentsAreSupported) {
    World world;
    const Entity first = world.Create();
    const Entity second = world.Create();
    world.Add<Owned>(first, std::make_unique<int>(1));
    world.Add<Owned>(second, std::make_unique<int>(2));

    world.Remove<Owned>(first);

    EXPECT_EQ(*world.Get<Owned>(second).value, 2);
}

TEST(Components, MissingComponentIsAbsent) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);

    EXPECT_FALSE(world.Has<Velocity>(entity));
    EXPECT_EQ(world.TryGet<Velocity>(entity), nullptr);
}

TEST(Components, ChangesThroughReferencesAreKept) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);

    world.Get<Position>(entity).x = 5.0f;

    EXPECT_EQ(world.Get<Position>(entity).x, 5.0f);
}

TEST(Components, RemoveReturnsWhetherAComponentWasRemoved) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);

    EXPECT_TRUE(world.Remove<Position>(entity));
    EXPECT_FALSE(world.Has<Position>(entity));
    EXPECT_FALSE(world.Remove<Position>(entity));
    EXPECT_FALSE(world.Remove<Velocity>(entity));
}

TEST(Components, RemovingKeepsTheOtherEntitiesComponents) {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 5; ++i) {
        entities.push_back(world.Create());
        world.Add<Position>(entities.back(), static_cast<float>(i), 0.0f);
    }

    world.Remove<Position>(entities[1]);
    world.Remove<Position>(entities[3]);

    EXPECT_EQ(world.Count<Position>(), 3u);
    EXPECT_EQ(world.Get<Position>(entities[0]).x, 0.0f);
    EXPECT_EQ(world.Get<Position>(entities[2]).x, 2.0f);
    EXPECT_EQ(world.Get<Position>(entities[4]).x, 4.0f);
}

TEST(Components, ComponentCanBeAddedAgainAfterRemoval) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity, 1.0f, 1.0f);
    world.Remove<Position>(entity);

    world.Add<Position>(entity, 2.0f, 2.0f);

    EXPECT_EQ(world.Get<Position>(entity).x, 2.0f);
}

TEST(Components, DestroyingAnEntityRemovesItsComponents) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);
    world.Add<Velocity>(entity);

    world.Destroy(entity);

    EXPECT_EQ(world.Count<Position>(), 0u);
    EXPECT_EQ(world.Count<Velocity>(), 0u);
}

TEST(Components, NewEntityInReusedSlotStartsWithoutComponents) {
    World world;
    const Entity old = world.Create();
    world.Add<Position>(old);
    world.Destroy(old);

    const Entity reused = world.Create();

    ASSERT_EQ(reused.Index(), old.Index());
    EXPECT_FALSE(world.Has<Position>(reused));
}

TEST(Components, StaleHandleDoesNotSeeTheNewEntitysComponents) {
    World world;
    const Entity old = world.Create();
    world.Destroy(old);
    const Entity reused = world.Create();
    world.Add<Position>(reused);

    EXPECT_FALSE(world.Has<Position>(old));
    EXPECT_EQ(world.TryGet<Position>(old), nullptr);
    EXPECT_FALSE(world.Remove<Position>(old));
    EXPECT_TRUE(world.Has<Position>(reused));
}

TEST(Components, WorldsAreIndependent) {
    World first;
    World second;
    const Entity a = first.Create();
    const Entity b = second.Create();
    first.Add<Position>(a);

    EXPECT_EQ(a, b); // Same slot in each world.
    EXPECT_FALSE(second.Has<Position>(b));
}

TEST(Components, MovedWorldKeepsItsEntities) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity, 3.0f, 0.0f);

    const World moved = std::move(world);

    EXPECT_TRUE(moved.IsAlive(entity));
    EXPECT_EQ(moved.Get<Position>(entity).x, 3.0f);
}

TEST(ComponentErrors, AddingToDeadEntityAsserts) {
    World world;
    const Entity entity = world.Create();
    world.Destroy(entity);
    DAI_EXPECT_ASSERT(world.Add<Position>(entity));
}

TEST(ComponentErrors, AddingTwiceAsserts) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);
    DAI_EXPECT_ASSERT(world.Add<Position>(entity));
}

TEST(ComponentErrors, GetOfMissingComponentAsserts) {
    World world;
    const Entity entity = world.Create();
    DAI_EXPECT_ASSERT(static_cast<void>(world.Get<Position>(entity)));
}

TEST(Each, VisitsOnlyEntitiesWithAllComponents) {
    World world;
    const Entity moving = world.Create();
    world.Add<Position>(moving);
    world.Add<Velocity>(moving);
    const Entity still = world.Create();
    world.Add<Position>(still);
    const Entity ghost = world.Create();
    world.Add<Velocity>(ghost);

    std::vector<Entity> visited;
    world.Each<Position, Velocity>([&](Entity entity, Position&, Velocity&) { visited.push_back(entity); });

    EXPECT_EQ(visited, std::vector<Entity>{moving});
}

TEST(Each, SingleComponentVisitsEveryOwner) {
    World world;
    for (int i = 0; i < 10; ++i) {
        world.Add<Position>(world.Create());
    }
    int count = 0;
    world.Each<Position>([&](Entity, Position&) { ++count; });
    EXPECT_EQ(count, 10);
}

TEST(Each, SystemsCanUpdateComponents) {
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity, 1.0f, 1.0f);
    world.Add<Velocity>(entity, 2.0f, -1.0f);

    world.Each<Position, Velocity>([](Entity, Position& position, Velocity& velocity) {
        position.x += velocity.x;
        position.y += velocity.y;
    });

    EXPECT_EQ(world.Get<Position>(entity).x, 3.0f);
    EXPECT_EQ(world.Get<Position>(entity).y, 0.0f);
}

TEST(Each, NeverStoredComponentVisitsNothing) {
    World world;
    world.Add<Position>(world.Create());
    int count = 0;
    world.Each<Position, Velocity>([&](Entity, Position&, Velocity&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(Each, DestroyedEntitiesAreNotVisited) {
    World world;
    const Entity kept = world.Create();
    world.Add<Position>(kept);
    const Entity destroyed = world.Create();
    world.Add<Position>(destroyed);
    world.Destroy(destroyed);

    std::vector<Entity> visited;
    world.Each<Position>([&](Entity entity, Position&) { visited.push_back(entity); });

    EXPECT_EQ(visited, std::vector<Entity>{kept});
}

TEST(Each, OtherComponentTypesCanBeAddedDuringIteration) {
    World world;
    for (int i = 0; i < 3; ++i) {
        world.Add<Position>(world.Create());
    }
    world.Each<Position>([&](Entity entity, Position&) { world.Add<Velocity>(entity); });
    EXPECT_EQ(world.Count<Velocity>(), 3u);
}

TEST(EachErrors, AddingAnIteratedComponentAsserts) {
    World world;
    world.Add<Position>(world.Create());
    const Entity other = world.Create();
    DAI_EXPECT_ASSERT(world.Each<Position>([&](Entity, Position&) { world.Add<Position>(other); }));
}

TEST(EachErrors, RemovingAnIteratedComponentAsserts) {
    World world;
    world.Add<Position>(world.Create());
    DAI_EXPECT_ASSERT(world.Each<Position>([&](Entity entity, Position&) { world.Remove<Position>(entity); }));
}

TEST(EachErrors, DestroyingAnIteratedEntityAsserts) {
    World world;
    world.Add<Position>(world.Create());
    DAI_EXPECT_ASSERT(world.Each<Position>([&](Entity entity, Position&) { world.Destroy(entity); }));
}

TEST(EachErrors, IterationCanContinueAfterAnAbortedLoop) {
    // The pools are unlocked when an iteration is left by an exception.
    if (!DAI_ASSERTS_ENABLED) {
        GTEST_SKIP() << "Assertions are disabled in this configuration";
    }
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);
    DAI_EXPECT_ASSERT(world.Each<Position>([&](Entity current, Position&) { world.Remove<Position>(current); }));

    EXPECT_TRUE(world.Remove<Position>(entity));
}

TEST(Entities, EachEntityVisitsTheLiveOnesByIndex) {
    World world;
    const Entity first = world.Create();
    const Entity second = world.Create();
    const Entity third = world.Create();
    world.Destroy(second);
    const Entity reused = world.Create();

    std::vector<Entity> visited;
    world.EachEntity([&visited](Entity entity) { visited.push_back(entity); });
    EXPECT_EQ(visited, (std::vector<Entity>{first, reused, third}));
}
