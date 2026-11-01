// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/WorkerPool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

namespace daidalos::core {
namespace {

void WaitFor(const std::atomic<bool>& flag) {
    while (!flag) {
        std::this_thread::yield();
    }
}

TEST(WorkerPool, StartsAtLeastOneThread) {
    const WorkerPool pool(0);
    EXPECT_EQ(pool.ThreadCount(), 1u);
}

TEST(WorkerPool, WorksOnAWorkerAndDeliversOnTheCallingThread) {
    WorkerPool pool(2);
    std::thread::id workThread;
    std::thread::id doneThread;
    int value = 0;
    pool.Submit(
        [&workThread] {
            workThread = std::this_thread::get_id();
            return 42;
        },
        [&](int result) {
            doneThread = std::this_thread::get_id();
            value = result;
        });
    pool.Finish();
    EXPECT_EQ(value, 42);
    EXPECT_NE(workThread, std::this_thread::get_id());
    EXPECT_EQ(doneThread, std::this_thread::get_id());
}

TEST(WorkerPool, NothingIsDeliveredBeforeDeliver) {
    WorkerPool pool(1);
    std::atomic<bool> release = false;
    bool delivered = false;
    pool.Submit(
        [&release] {
            WaitFor(release);
            return 1;
        },
        [&delivered](int /*result*/) { delivered = true; });
    pool.Deliver();
    EXPECT_FALSE(delivered);
    EXPECT_EQ(pool.PendingCount(), 1u);
    release = true;
    pool.Finish();
    EXPECT_TRUE(delivered);
    EXPECT_EQ(pool.PendingCount(), 0u);
}

TEST(WorkerPool, AcceptsMoveOnlyResults) {
    WorkerPool pool(1);
    int value = 0;
    pool.Submit([] { return std::make_unique<int>(7); }, [&value](std::unique_ptr<int> result) { value = *result; });
    pool.Finish();
    EXPECT_EQ(value, 7);
}

TEST(WorkerPool, RunsManyJobs) {
    WorkerPool pool(4);
    int sum = 0;
    for (int index = 1; index <= 100; ++index) {
        pool.Submit([index] { return index; }, [&sum](int result) { sum += result; });
    }
    pool.Finish();
    EXPECT_EQ(sum, 5050);
}

TEST(WorkerPool, QueuedJobCancelledIsNeverRun) {
    WorkerPool pool(1);
    std::atomic<bool> release = false;
    std::atomic<bool> started = false;
    pool.Submit(
        [&] {
            started = true;
            WaitFor(release);
            return 0;
        },
        [](int /*result*/) {});
    WaitFor(started);

    std::atomic<bool> ran = false;
    bool delivered = false;
    const JobId queued = pool.Submit(
        [&ran] {
            ran = true;
            return 0;
        },
        [&delivered](int /*result*/) { delivered = true; });
    pool.Cancel(queued);
    release = true;
    pool.Finish();
    EXPECT_FALSE(ran);
    EXPECT_FALSE(delivered);
}

TEST(WorkerPool, RunningJobCancelledIsNotDelivered) {
    WorkerPool pool(1);
    std::atomic<bool> release = false;
    std::atomic<bool> started = false;
    bool delivered = false;
    const JobId job = pool.Submit(
        [&] {
            started = true;
            WaitFor(release);
            return 0;
        },
        [&delivered](int /*result*/) { delivered = true; });
    WaitFor(started);
    pool.Cancel(job);
    release = true;
    pool.Finish();
    EXPECT_FALSE(delivered);
    EXPECT_EQ(pool.PendingCount(), 0u);
}

TEST(WorkerPool, FinishedJobCancelledBeforeDeliverIsNotDelivered) {
    WorkerPool pool(1);
    bool delivered = false;
    const JobId job = pool.Submit([] { return 0; }, [&delivered](int /*result*/) { delivered = true; });
    while (pool.PendingCount() > 0) {
        pool.Cancel(job);
    }
    pool.Deliver();
    EXPECT_FALSE(delivered);
}

TEST(WorkerPool, CancellingAnUnknownJobIsIgnored) {
    WorkerPool pool(1);
    pool.Cancel(12345);
    EXPECT_EQ(pool.PendingCount(), 0u);
}

TEST(WorkerPool, DoneFunctionCanSubmitAJob) {
    WorkerPool pool(1);
    int value = 0;
    pool.Submit(
        [] { return 1; },
        [&](int first) { pool.Submit([first] { return first + 1; }, [&value](int second) { value = second; }); });
    pool.Finish();
    pool.Finish();
    EXPECT_EQ(value, 2);
}

TEST(WorkerPool, DestructionDropsWhatHasNotBeenDelivered) {
    bool delivered = false;
    {
        WorkerPool pool(2);
        for (int index = 0; index < 20; ++index) {
            pool.Submit([] { return 0; }, [&delivered](int /*result*/) { delivered = true; });
        }
    }
    EXPECT_FALSE(delivered);
}

} // namespace
} // namespace daidalos::core
