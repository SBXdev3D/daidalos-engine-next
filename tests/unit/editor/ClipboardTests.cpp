// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
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

namespace {

TEST(Clipboard, APrefabTakesTheSelectionWithNewIdsAndPlacedRoots) {
    Level level = Sample();
    const std::vector<std::uint64_t> selection{2, 4};
    const Level prefab = editor::PrefabOf(level, selection, [](const LevelEntity& root) {
        math::Transform placed;
        placed.translation = {static_cast<float>(root.id), 0.0f, 0.0f};
        return placed;
    });
    ASSERT_EQ(prefab.entities.size(), 3U) << "b and c under it, and d";
    std::vector<std::uint64_t> ids;
    for (const LevelEntity& entity : prefab.entities) {
        ids.push_back(entity.id);
    }
    std::ranges::sort(ids);
    EXPECT_EQ(ids, (std::vector<std::uint64_t>{1, 2, 3}));
    const LevelEntity* b = nullptr;
    const LevelEntity* c = nullptr;
    const LevelEntity* d = nullptr;
    for (const LevelEntity& entity : prefab.entities) {
        (entity.name == "b" ? b : entity.name == "c" ? c : d) = &entity;
    }
    ASSERT_TRUE(b != nullptr && c != nullptr && d != nullptr);
    EXPECT_EQ(b->parent, 0U) << "A root of the selection is at the prefab's root";
    EXPECT_EQ(b->transform.translation.x, 2.0f);
    EXPECT_EQ(c->parent, b->id);
    EXPECT_EQ(c->transform, level.Find(3)->transform) << "What is under a root keeps its place in it";
    EXPECT_EQ(d->ribbon->target, c->id) << "References inside the prefab follow its ids";
}

TEST(Clipboard, UnpackingPutsThePrefabsEntitiesUnderItsEntity) {
    Level level;
    level.entities = {LevelEntity{.id = 1, .name = "alarm", .prefab = "alarm.prefab.json"}};
    Level prefab;
    prefab.entities = {LevelEntity{.id = 1, .name = "beam"}, LevelEntity{.id = 2, .name = "glow", .parent = 1}};
    const std::vector<std::uint64_t> roots = editor::Unpack(level, 1, prefab);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_TRUE(level.Find(1)->prefab.empty());
    EXPECT_EQ(level.entities.size(), 3U);
    const LevelEntity* beam = level.Find(roots[0]);
    ASSERT_NE(beam, nullptr);
    EXPECT_EQ(beam->name, "beam");
    EXPECT_EQ(beam->parent, 1U) << "Not taken for the prefab's own entity 1";
    for (const LevelEntity& entity : level.entities) {
        if (entity.name == "glow") {
            EXPECT_EQ(entity.parent, beam->id);
        }
    }
}

} // namespace
