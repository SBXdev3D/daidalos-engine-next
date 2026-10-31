// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/SlotPool.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_set>

using namespace daidalos::core;

namespace {

struct ItemTag;
using Pool = SlotPool<std::string, ItemTag>;
using ItemHandle = Pool::HandleType;

} // namespace

TEST(Handle, DefaultConstructedIsNull) {
    const ItemHandle handle;
    EXPECT_TRUE(handle.IsNull());
    EXPECT_FALSE(handle);
}

TEST(SlotPool, InsertedObjectIsFoundThroughItsHandle) {
    Pool pool;
    const ItemHandle handle = pool.Insert("mesh");
    EXPECT_TRUE(handle);
    ASSERT_NE(pool.TryGet(handle), nullptr);
    EXPECT_EQ(pool.Get(handle), "mesh");
    EXPECT_EQ(pool.Size(), 1U);
}

TEST(SlotPool, NullHandleIsAbsent) {
    Pool pool;
    pool.Insert("mesh");
    EXPECT_EQ(pool.TryGet(ItemHandle{}), nullptr);
    EXPECT_FALSE(pool.Remove(ItemHandle{}).has_value());
}

TEST(SlotPool, RemoveReturnsTheObjectOnce) {
    Pool pool;
    const ItemHandle handle = pool.Insert("mesh");
    EXPECT_EQ(pool.Remove(handle), "mesh");
    EXPECT_FALSE(pool.Remove(handle).has_value());
    EXPECT_EQ(pool.Size(), 0U);
}

TEST(SlotPool, StaleHandleIsAbsentAfterTheSlotIsReused) {
    Pool pool;
    const ItemHandle stale = pool.Insert("old");
    pool.Remove(stale);
    const ItemHandle fresh = pool.Insert("new");

    EXPECT_EQ(fresh.Index(), stale.Index());
    EXPECT_NE(fresh, stale);
    EXPECT_EQ(pool.TryGet(stale), nullptr);
    EXPECT_EQ(pool.Get(fresh), "new");
}

TEST(SlotPool, HoldsMoveOnlyObjects) {
    struct OwnerTag;
    SlotPool<std::unique_ptr<int>, OwnerTag> pool;
    const auto handle = pool.Insert(std::make_unique<int>(7));
    EXPECT_EQ(*pool.Get(handle), 7);
    EXPECT_EQ(**pool.Remove(handle), 7);
}

TEST(SlotPool, ForEachVisitsLiveObjectsOnly) {
    Pool pool;
    pool.Insert("a");
    const ItemHandle removed = pool.Insert("b");
    pool.Insert("c");
    pool.Remove(removed);

    std::string visited;
    pool.ForEach([&visited](const std::string& item) { visited += item; });
    EXPECT_EQ(visited, "ac");
}

TEST(Handle, HashesDistinguishGenerations) {
    std::unordered_set<ItemHandle> handles;
    handles.insert(ItemHandle(1, 1));
    handles.insert(ItemHandle(1, 2));
    EXPECT_EQ(handles.size(), 2U);
}
