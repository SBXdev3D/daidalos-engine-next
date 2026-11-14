// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Schedule.hpp>
#include <daidalos/ecs/World.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "AssertCapture.hpp"

using namespace daidalos;
using ecs::Entity;
using ecs::Schedule;
using ecs::World;

namespace {

struct Position {
    float x = 0.0f;
};

struct Velocity {
    float x = 0.0f;
};

} // namespace

TEST(Schedule, RunsSystemsInInsertionOrder) {
    Schedule schedule;
    std::vector<std::string> journal;
    schedule.Add("input", [&](World&, core::Seconds) { journal.emplace_back("input"); });
    schedule.Add("movement", [&](World&, core::Seconds) { journal.emplace_back("movement"); });
    schedule.Add("transforms", [&](World&, core::Seconds) { journal.emplace_back("transforms"); });

    World world;
    schedule.Run(world, core::Seconds{0.0});

    EXPECT_EQ(journal, (std::vector<std::string>{"input", "movement", "transforms"}));
}

TEST(Schedule, PassesWorldAndDelta) {
    Schedule schedule;
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);
    world.Add<Velocity>(entity, 2.0f);
    schedule.Add("movement", [](World& target, core::Seconds delta) {
        target.Each<Position, Velocity>([&](Entity, Position& position, Velocity& velocity) {
            position.x += velocity.x * static_cast<float>(delta.count());
        });
    });

    schedule.Run(world, core::Seconds{0.5});
    schedule.Run(world, core::Seconds{0.5});

    EXPECT_FLOAT_EQ(world.Get<Position>(entity).x, 2.0f);
}

TEST(Schedule, LaterSystemsSeeEarlierChanges) {
    Schedule schedule;
    World world;
    const Entity entity = world.Create();
    world.Add<Position>(entity);
    float seen = -1.0f;
    schedule.Add("write", [&](World& target, core::Seconds) { target.Get<Position>(entity).x = 7.0f; });
    schedule.Add("read", [&](World& target, core::Seconds) { seen = target.Get<Position>(entity).x; });

    schedule.Run(world, core::Seconds{0.0});

    EXPECT_EQ(seen, 7.0f);
}

TEST(Schedule, NamesAreListedInOrder) {
    Schedule schedule;
    schedule.Add("b", [](World&, core::Seconds) {});
    schedule.Add("a", [](World&, core::Seconds) {});
    EXPECT_EQ(schedule.Size(), 2u);
    EXPECT_EQ(schedule.Names(), (std::vector<std::string_view>{"b", "a"}));
}

TEST(Schedule, EmptyScheduleRuns) {
    const Schedule schedule;
    World world;
    schedule.Run(world, core::Seconds{0.016});
    EXPECT_EQ(schedule.Size(), 0u);
}

TEST(ScheduleErrors, DuplicateNameAsserts) {
    Schedule schedule;
    schedule.Add("movement", [](World&, core::Seconds) {});
    DAI_EXPECT_ASSERT(schedule.Add("movement", [](World&, core::Seconds) {}));
}

TEST(ScheduleErrors, EmptyFunctionAsserts) {
    Schedule schedule;
    DAI_EXPECT_ASSERT(schedule.Add("empty", nullptr));
}
