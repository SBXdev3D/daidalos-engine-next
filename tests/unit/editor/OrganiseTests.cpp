// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>

#include "Organise.hpp"

using namespace daidalos;
using namespace editor;

namespace {

/// 1 holds 2, which holds 3; 4 stands alone; 5 is in a folder.
scene::Level Family() {
    scene::Level level;
    level.entities.push_back(scene::LevelEntity{.id = 1, .name = "Shelf"});
    level.entities.push_back(scene::LevelEntity{.id = 2, .name = "Box", .parent = 1});
    level.entities.push_back(scene::LevelEntity{.id = 3, .name = "Can", .parent = 2});
    level.entities.push_back(scene::LevelEntity{.id = 4, .name = "Lamp"});
    scene::LevelEntity filed{.id = 5, .name = "Fan"};
    filed.editing.folder = "Props/Hall";
    level.entities.push_back(filed);
    return level;
}

std::vector<std::uint64_t> Ids(const scene::Level& level) {
    std::vector<std::uint64_t> ids;
    for (const scene::LevelEntity& entity : level.entities) {
        ids.push_back(entity.id);
    }
    return ids;
}

} // namespace

TEST(OrganiseTest, HidingTakesWhatIsUnderToo) {
    scene::Level level = Family();
    level.Find(2)->editing.hidden = true;
    EXPECT_EQ(Ids(ShownLevel(level, {})), (std::vector<std::uint64_t>{1, 4, 5}));
    EXPECT_TRUE(HiddenInEditor(level, 3));
    EXPECT_FALSE(HiddenInEditor(level, 1));
}

TEST(OrganiseTest, IsolatingKeepsWhatHoldsAndWhatIsUnder) {
    const scene::Level level = Family();
    EXPECT_EQ(Ids(ShownLevel(level, {2})), (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(Ids(ShownLevel(level, {4, 5})), (std::vector<std::uint64_t>{4, 5}));
}

TEST(OrganiseTest, LocksPassDown) {
    scene::Level level = Family();
    level.Find(1)->editing.locked = true;
    EXPECT_TRUE(LockedInEditor(level, 3));
    EXPECT_FALSE(LockedInEditor(level, 4));
}

TEST(OrganiseTest, AParentCycleDoesNotHang) {
    scene::Level level;
    level.entities.push_back(scene::LevelEntity{.id = 1, .parent = 2});
    level.entities.push_back(scene::LevelEntity{.id = 2, .parent = 1});
    EXPECT_FALSE(HiddenInEditor(level, 1));
}

TEST(OrganiseTest, FoldersAreTidiedAndListedWithTheirParents) {
    EXPECT_EQ(TidyFolder(" Lights // Hall/ "), "Lights/Hall");
    EXPECT_EQ(TidyFolder("a\\b"), "a/b");
    EXPECT_EQ(TidyFolder(" / "), "");
    EXPECT_EQ(FoldersOf(Family()), (std::vector<std::string>{"Props", "Props/Hall"}));
    EXPECT_EQ(ParentFolder("Props/Hall"), "Props");
    EXPECT_EQ(ParentFolder("Props"), "");
    EXPECT_TRUE(InFolder("Props/Hall", "Props"));
    EXPECT_TRUE(InFolder("Props", "Props"));
    EXPECT_FALSE(InFolder("Props2", "Props"));
}

TEST(OrganiseTest, AlignLinesUpOneAxis) {
    const std::vector<glm::vec3> positions{{1.0f, 0.0f, 5.0f}, {3.0f, 2.0f, -1.0f}, {2.0f, 1.0f, 0.0f}};
    const std::vector<glm::vec3> lowest = Aligned(positions, 0, AlignTo::Min);
    EXPECT_FLOAT_EQ(lowest[1].x, 1.0f);
    EXPECT_FLOAT_EQ(lowest[1].y, 2.0f);
    const std::vector<glm::vec3> middle = Aligned(positions, 1, AlignTo::Center);
    EXPECT_FLOAT_EQ(middle[0].y, 1.0f);
    EXPECT_FLOAT_EQ(Aligned(positions, 2, AlignTo::Max)[2].z, 5.0f);
}

TEST(OrganiseTest, SpreadKeepsTheEndsAndTheOrder) {
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f}, {9.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
    const std::vector<glm::vec3> spread = Spread(positions, 0);
    EXPECT_FLOAT_EQ(spread[0].x, 0.0f);
    EXPECT_FLOAT_EQ(spread[1].x, 9.0f);
    EXPECT_FLOAT_EQ(spread[2].x, 3.0f);
    EXPECT_FLOAT_EQ(spread[3].x, 6.0f);
}

TEST(OrganiseTest, PivotsSitOnTheSelection) {
    Bounds bounds;
    bounds.Add(glm::vec3(0.0f, 1.0f, 0.0f));
    bounds.Add(glm::vec3(2.0f, 3.0f, 4.0f));
    const glm::vec3 origin(9.0f);
    EXPECT_EQ(PivotPoint(Pivot::Origin, origin, bounds), origin);
    EXPECT_EQ(PivotPoint(Pivot::Center, origin, bounds), glm::vec3(1.0f, 2.0f, 2.0f));
    EXPECT_EQ(PivotPoint(Pivot::Bottom, origin, bounds), glm::vec3(1.0f, 1.0f, 2.0f));
    EXPECT_EQ(PivotPoint(Pivot::Center, origin, Bounds{}), origin);
}
