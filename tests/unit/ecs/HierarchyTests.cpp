// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/World.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace daidalos;
using ecs::Entity;
using ecs::World;

namespace {

struct Tag {
    int value = 0;
};

std::vector<Entity> Children(const World& world, Entity parent) {
    std::vector<Entity> children;
    for (Entity child = world.FirstChild(parent); !child.IsNull(); child = world.NextSibling(child)) {
        children.push_back(child);
    }
    return children;
}

} // namespace

TEST(Hierarchy, NewEntityHasNoParentOrChildren) {
    World world;
    const Entity entity = world.Create();
    EXPECT_TRUE(world.Parent(entity).IsNull());
    EXPECT_TRUE(world.FirstChild(entity).IsNull());
    EXPECT_EQ(world.ChildCount(entity), 0u);
}

TEST(Hierarchy, SetParentLinksBothWays) {
    World world;
    const Entity parent = world.Create();
    const Entity child = world.Create();

    ASSERT_TRUE(world.SetParent(child, parent).has_value());

    EXPECT_EQ(world.Parent(child), parent);
    EXPECT_EQ(Children(world, parent), std::vector<Entity>{child});
    EXPECT_EQ(world.ChildCount(parent), 1u);
}

TEST(Hierarchy, ChildrenKeepAttachmentOrder) {
    World world;
    const Entity parent = world.Create();
    const Entity a = world.Create();
    const Entity b = world.Create();
    const Entity c = world.Create();
    ASSERT_TRUE(world.SetParent(a, parent).has_value());
    ASSERT_TRUE(world.SetParent(b, parent).has_value());
    ASSERT_TRUE(world.SetParent(c, parent).has_value());

    EXPECT_EQ(Children(world, parent), (std::vector<Entity>{a, b, c}));
    EXPECT_EQ(world.ChildCount(parent), 3u);
}

TEST(Hierarchy, DetachingMiddleChildKeepsSiblingsLinked) {
    World world;
    const Entity parent = world.Create();
    const Entity a = world.Create();
    const Entity b = world.Create();
    const Entity c = world.Create();
    for (const Entity child : {a, b, c}) {
        ASSERT_TRUE(world.SetParent(child, parent).has_value());
    }

    ASSERT_TRUE(world.SetParent(b, ecs::kNullEntity).has_value());

    EXPECT_TRUE(world.Parent(b).IsNull());
    EXPECT_EQ(Children(world, parent), (std::vector<Entity>{a, c}));
    EXPECT_EQ(world.ChildCount(parent), 2u);
}

TEST(Hierarchy, DetachingFirstAndLastChildren) {
    World world;
    const Entity parent = world.Create();
    const Entity a = world.Create();
    const Entity b = world.Create();
    const Entity c = world.Create();
    for (const Entity child : {a, b, c}) {
        ASSERT_TRUE(world.SetParent(child, parent).has_value());
    }

    ASSERT_TRUE(world.SetParent(a, ecs::kNullEntity).has_value());
    ASSERT_TRUE(world.SetParent(c, ecs::kNullEntity).has_value());
    EXPECT_EQ(Children(world, parent), std::vector<Entity>{b});

    // The list still accepts new children at the end.
    ASSERT_TRUE(world.SetParent(a, parent).has_value());
    EXPECT_EQ(Children(world, parent), (std::vector<Entity>{b, a}));
}

TEST(Hierarchy, ReparentingMovesTheChild) {
    World world;
    const Entity first = world.Create();
    const Entity second = world.Create();
    const Entity child = world.Create();
    ASSERT_TRUE(world.SetParent(child, first).has_value());

    ASSERT_TRUE(world.SetParent(child, second).has_value());

    EXPECT_EQ(world.Parent(child), second);
    EXPECT_TRUE(Children(world, first).empty());
    EXPECT_EQ(Children(world, second), std::vector<Entity>{child});
}

TEST(Hierarchy, SettingTheSameParentAgainChangesNothing) {
    World world;
    const Entity parent = world.Create();
    const Entity a = world.Create();
    const Entity b = world.Create();
    ASSERT_TRUE(world.SetParent(a, parent).has_value());
    ASSERT_TRUE(world.SetParent(b, parent).has_value());

    ASSERT_TRUE(world.SetParent(a, parent).has_value());

    EXPECT_EQ(Children(world, parent), (std::vector<Entity>{a, b}));
}

TEST(Hierarchy, IsDescendantOfFollowsTheWholeChain) {
    World world;
    const Entity root = world.Create();
    const Entity middle = world.Create();
    const Entity leaf = world.Create();
    ASSERT_TRUE(world.SetParent(middle, root).has_value());
    ASSERT_TRUE(world.SetParent(leaf, middle).has_value());

    EXPECT_TRUE(world.IsDescendantOf(leaf, root));
    EXPECT_TRUE(world.IsDescendantOf(leaf, middle));
    EXPECT_FALSE(world.IsDescendantOf(root, leaf));
    EXPECT_FALSE(world.IsDescendantOf(leaf, leaf));
}

TEST(HierarchyErrors, AttachingToItselfFails) {
    World world;
    const Entity entity = world.Create();

    const core::Result<void> result = world.SetParent(entity, entity);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().module, "ecs");
    EXPECT_TRUE(world.Parent(entity).IsNull());
}

TEST(HierarchyErrors, CycleIsRejectedAndNothingChanges) {
    World world;
    const Entity root = world.Create();
    const Entity middle = world.Create();
    const Entity leaf = world.Create();
    ASSERT_TRUE(world.SetParent(middle, root).has_value());
    ASSERT_TRUE(world.SetParent(leaf, middle).has_value());

    const core::Result<void> result = world.SetParent(root, leaf);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(result.error().message.find("cycle"), std::string::npos) << result.error().message;
    EXPECT_TRUE(world.Parent(root).IsNull());
    EXPECT_EQ(world.Parent(leaf), middle);
    EXPECT_EQ(world.Parent(middle), root);
}

TEST(HierarchyErrors, DeadParentIsRejected) {
    World world;
    const Entity parent = world.Create();
    const Entity child = world.Create();
    world.Destroy(parent);

    const core::Result<void> result = world.SetParent(child, parent);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(world.Parent(child).IsNull());
}

TEST(HierarchyErrors, DeadChildIsRejected) {
    World world;
    const Entity parent = world.Create();
    const Entity child = world.Create();
    world.Destroy(child);

    EXPECT_FALSE(world.SetParent(child, parent).has_value());
    EXPECT_EQ(world.ChildCount(parent), 0u);
}

TEST(HierarchyErrors, StaleHandleSeesNoRelations) {
    World world;
    const Entity parent = world.Create();
    const Entity child = world.Create();
    ASSERT_TRUE(world.SetParent(child, parent).has_value());
    world.Destroy(child);
    const Entity reused = world.Create();
    ASSERT_EQ(reused.Index(), child.Index());

    EXPECT_TRUE(world.Parent(child).IsNull());
    EXPECT_TRUE(world.Parent(reused).IsNull());
    EXPECT_FALSE(world.IsDescendantOf(child, parent));
}

TEST(HierarchyDestruction, DestroyingAParentDestroysItsDescendants) {
    World world;
    const Entity root = world.Create();
    const Entity child = world.Create();
    const Entity grandchild = world.Create();
    const Entity unrelated = world.Create();
    ASSERT_TRUE(world.SetParent(child, root).has_value());
    ASSERT_TRUE(world.SetParent(grandchild, child).has_value());
    world.Add<Tag>(grandchild);

    EXPECT_TRUE(world.Destroy(root));

    EXPECT_FALSE(world.IsAlive(root));
    EXPECT_FALSE(world.IsAlive(child));
    EXPECT_FALSE(world.IsAlive(grandchild));
    EXPECT_TRUE(world.IsAlive(unrelated));
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_EQ(world.Count<Tag>(), 0u);
}

TEST(HierarchyDestruction, DestroyingAChildDetachesItFromItsParent) {
    World world;
    const Entity parent = world.Create();
    const Entity a = world.Create();
    const Entity b = world.Create();
    ASSERT_TRUE(world.SetParent(a, parent).has_value());
    ASSERT_TRUE(world.SetParent(b, parent).has_value());

    world.Destroy(a);

    EXPECT_TRUE(world.IsAlive(parent));
    EXPECT_EQ(Children(world, parent), std::vector<Entity>{b});
    EXPECT_EQ(world.ChildCount(parent), 1u);
}

TEST(HierarchyDestruction, SlotsOfDestroyedSubtreeAreReusedCleanly) {
    World world;
    const Entity root = world.Create();
    const Entity child = world.Create();
    ASSERT_TRUE(world.SetParent(child, root).has_value());
    world.Destroy(root);

    const Entity a = world.Create();
    const Entity b = world.Create();

    EXPECT_TRUE(world.Parent(a).IsNull());
    EXPECT_TRUE(world.Parent(b).IsNull());
    EXPECT_EQ(world.ChildCount(a), 0u);
    EXPECT_EQ(world.ChildCount(b), 0u);
}

TEST(HierarchyDestruction, WideAndDeepTreesAreDestroyedCompletely) {
    World world;
    const Entity root = world.Create();
    Entity parent = root;
    for (int depth = 0; depth < 100; ++depth) {
        for (int sibling = 0; sibling < 3; ++sibling) {
            ASSERT_TRUE(world.SetParent(world.Create(), parent).has_value());
        }
        parent = world.FirstChild(parent);
    }
    EXPECT_EQ(world.AliveCount(), 301u);

    world.Destroy(root);

    EXPECT_EQ(world.AliveCount(), 0u);
}
