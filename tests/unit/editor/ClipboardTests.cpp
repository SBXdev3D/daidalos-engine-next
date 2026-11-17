// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include "Clipboard.hpp"

using namespace daidalos;
using scene::Level;
using scene::LevelEntity;

namespace {

// 1 > 2 > 3, and 4 at the root, whose ribbon aims at 3.
Level Sample() {
    Level level;
    level.entities = {LevelEntity{.id = 1, .name = "a"}, LevelEntity{.id = 2, .name = "b", .parent = 1},
                      LevelEntity{.id = 3, .name = "c", .parent = 2}, LevelEntity{.id = 4, .name = "d"}};
    level.entities[3].ribbon = scene::LevelRibbon{.effect = "arc.ribbon.json", .target = 3};
    return level;
}

TEST(Clipboard, CopiesWhatIsUnderTheSelectionOnce) {
    const Level level = Sample();
    const std::vector<std::uint64_t> selection{2, 3};
    const std::vector<LevelEntity> copies = editor::CopyEntities(level, selection);
    ASSERT_EQ(copies.size(), 2U);
    EXPECT_EQ(copies[0].id, 2U);
    EXPECT_EQ(copies[1].id, 3U);
    EXPECT_EQ(editor::CopiedRoots(copies), std::vector<std::uint64_t>{2});
}

TEST(Clipboard, PastedCopiesTakeNewIdsAndFollowEachOther) {
    Level level = Sample();
    const std::vector<std::uint64_t> selection{1, 4};
    const std::vector<std::uint64_t> roots = editor::PasteEntities(level, editor::CopyEntities(level, selection));
    ASSERT_EQ(level.entities.size(), 8U);
    ASSERT_EQ(roots.size(), 2U);
    const LevelEntity* a = level.Find(roots[0]);
    const LevelEntity* d = level.Find(roots[1]);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(a->name, "a");
    EXPECT_EQ(a->parent, 0U);
    EXPECT_GT(a->id, 4U);
    // The copy of 4 aims at the copy of 3, not at 3.
    const LevelEntity* target = level.Find(d->ribbon->target);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->name, "c");
    EXPECT_NE(target->id, 3U);
    EXPECT_EQ(level.Find(level.Find(target->parent)->parent)->id, a->id);
}

TEST(Clipboard, AReferenceOutsideTheCopyStaysIfTheLevelHasIt) {
    Level level = Sample();
    const std::vector<std::uint64_t> selection{4};
    std::vector<LevelEntity> copies = editor::CopyEntities(level, selection);
    editor::PasteEntities(level, copies);
    EXPECT_EQ(level.entities.back().ribbon->target, 3U) << "3 is in the level";

    Level other;
    editor::PasteEntities(other, copies);
    EXPECT_EQ(other.entities.back().ribbon->target, 0U) << "Another level has no 3";
    EXPECT_EQ(other.entities.back().id, 1U);
}

TEST(Clipboard, AChildPastedAloneKeepsItsParentIfThereIsOne) {
    Level level = Sample();
    const std::vector<std::uint64_t> selection{3};
    editor::PasteEntities(level, editor::CopyEntities(level, selection));
    EXPECT_EQ(level.entities.back().parent, 2U);
}

TEST(Clipboard, KnowsAncestors) {
    const Level level = Sample();
    EXPECT_TRUE(editor::IsAncestor(level, 1, 3));
    EXPECT_TRUE(editor::IsAncestor(level, 3, 3));
    EXPECT_FALSE(editor::IsAncestor(level, 3, 1));
    EXPECT_FALSE(editor::IsAncestor(level, 4, 3));
}

} // namespace
