// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <limits>
#include <optional>

#include "History.hpp"

using namespace daidalos;
using History = editor::UndoHistory<daidalos::scene::Level>;
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

TEST(History, MaterialAndSequenceEditsAreStepsOfTheDocument) {
    editor::History history;
    editor::Document document;
    const editor::MaterialKey key{nullptr, 2};
    document.materials[key].roughnessFactor = 0.5f;
    document.sequencePath = "Sequences/scare.sequence.json";

    history.Begin(document, "Edit a material");
    editor::Document after = document;
    after.materials[key].roughnessFactor = 0.9f;
    history.Commit(after);
    ASSERT_TRUE(history.CanUndo());

    history.Begin(after, "Edit the sequence");
    editor::Document later = after;
    later.sequence.cues.push_back(daidalos::scene::Cue{});
    history.Commit(later);

    std::optional<editor::Document> back = history.Undo(later);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->sequence.cues.empty());
    back = history.Undo(*back);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->materials.at(key).roughnessFactor, 0.5f);
}

TEST(History, MaterialsCompareByTheirValues) {
    daidalos::resources::MaterialData a;
    daidalos::resources::MaterialData b;
    a.attenuationDistance = std::numeric_limits<float>::infinity();
    b.attenuationDistance = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(editor::SameValues(a, b));
    b.metallicFactor = 0.0f;
    EXPECT_FALSE(editor::SameValues(a, b));
    editor::Document first;
    editor::Document second;
    first.materials[{nullptr, 0}] = a;
    second.materials[{nullptr, 0}] = a;
    EXPECT_EQ(first, second);
    second.materials[{nullptr, 0}] = b;
    EXPECT_FALSE(first == second);
}
