// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include "History.hpp"

using namespace daidalos;
using editor::History;
using scene::Level;
using scene::LevelEntity;

namespace {

Level WithEntities(std::size_t count) {
    Level level;
    for (std::size_t index = 0; index < count; ++index) {
        level.entities.push_back(LevelEntity{.id = index + 1});
    }
    return level;
}

TEST(History, UndoAndRedoWalkTheSteps) {
    History history;
    Level level = WithEntities(1);
    history.Begin(level, "Add");
    level.entities.push_back(LevelEntity{.id = 2});
    history.Commit(level);
    history.Begin(level, "Move");
    level.entities[0].transform.translation.x = 3.0f;
    history.Commit(level);
    EXPECT_EQ(history.Steps(), 2U);
    EXPECT_EQ(history.UndoLabel(), "Move");

    std::optional<Level> back = history.Undo(level);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->entities[0].transform.translation.x, 0.0f);
    EXPECT_EQ(back->entities.size(), 2U);
    EXPECT_EQ(history.RedoLabel(), "Move");
    back = history.Undo(*back);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->entities.size(), 1U);
    EXPECT_FALSE(history.Undo(*back).has_value());

    std::optional<Level> forward = history.Redo(*back);
    forward = history.Redo(*forward);
    ASSERT_TRUE(forward.has_value());
    EXPECT_EQ(*forward, level);
    EXPECT_FALSE(history.CanRedo());
}

TEST(History, AnOpenEditIsOneStepHoweverManyChanges) {
    History history;
    Level level = WithEntities(1);
    for (int frame = 0; frame < 30; ++frame) {
        history.Begin(level, "Move");
        level.entities[0].transform.translation.x += 0.1f;
    }
    history.Commit(level);
    EXPECT_EQ(history.Steps(), 1U);
    const std::optional<Level> back = history.Undo(level);
    EXPECT_EQ(back->entities[0].transform.translation.x, 0.0f);
}

TEST(History, AnEditThatChangesNothingIsNoStep) {
    History history;
    const Level level = WithEntities(2);
    history.Begin(level, "Nothing");
    history.Commit(level);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_EQ(history.Current(), 0U);
}

TEST(History, ANewEditForgetsWhatCouldBeRedone) {
    History history;
    Level level = WithEntities(1);
    history.Begin(level, "A");
    level.entities[0].name = "a";
    history.Commit(level);
    level = *history.Undo(level);
    EXPECT_TRUE(history.CanRedo());
    history.Begin(level, "B");
    level.entities[0].name = "b";
    history.Commit(level);
    EXPECT_FALSE(history.CanRedo());
}

TEST(History, RevisionsComeBackWithTheirState) {
    History history;
    Level level = WithEntities(1);
    const History::Revision saved = history.Current();
    history.Begin(level, "A");
    level.entities[0].name = "a";
    history.Commit(level);
    EXPECT_NE(history.Current(), saved) << "The level changed since it was saved";
    level = *history.Undo(level);
    EXPECT_EQ(history.Current(), saved) << "Undone back to what was saved";
    level = *history.Redo(level);
    EXPECT_NE(history.Current(), saved);
}

TEST(History, KeepsALimitedNumberOfSteps) {
    History history;
    Level level = WithEntities(1);
    for (std::size_t step = 0; step < History::kMaxSteps + 10; ++step) {
        history.Begin(level, "Rename");
        level.entities[0].name = std::to_string(step);
        history.Commit(level);
    }
    EXPECT_EQ(history.Steps(), History::kMaxSteps);
}

} // namespace
