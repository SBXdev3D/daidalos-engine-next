// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace daidalos::core {

/// Identifies a submitted job, to cancel it. Never reused by a pool.
using JobId = std::uint64_t;

/// The worker threads of ADR-0011. A job runs on a worker and must not touch the ECS, the renderer or
/// anything else that belongs to the main thread. Its result comes back through a function called by
/// Deliver, on the main thread. A cancelled job is never delivered.
///
/// Destroying the pool cancels what has not started and waits for what is running.
class WorkerPool {
public:
    /// At least one thread is started.
    explicit WorkerPool(std::uint32_t threadCount);
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    ~WorkerPool();

    /// Runs work() on a worker, then done(result) in a later call to Deliver.
    template <class Work, class Done>
    JobId Submit(Work work, Done done) {
        using Value = std::invoke_result_t<Work&>;
        // std::function requires copyable functions, and results are often move-only.
        auto shared = std::make_shared<Work>(std::move(work));
        auto callback = std::make_shared<Done>(std::move(done));
        return Enqueue([shared, callback]() -> std::function<void()> {
            auto result = std::make_shared<Value>((*shared)());
            return [callback, result] { (*callback)(std::move(*result)); };
        });
    }

    /// Whether it had started or not, the job's result will not be delivered. A job that is running
    /// still finishes. Unknown and delivered jobs are ignored.
    void Cancel(JobId job);

    /// Calls the done functions of the jobs that have finished. Main thread only.
    void Deliver();

    /// Jobs that are queued, running or waiting for Deliver.
    [[nodiscard]] std::size_t PendingCount() const;
    /// Waits until every job has finished, then delivers them. For tests and shutdown.
    void Finish();

    [[nodiscard]] std::size_t ThreadCount() const { return m_threads.size(); }

private:
    struct Queued {
        JobId id;
        std::function<std::function<void()>()> work;
    };
    struct Finished {
        JobId id;
        std::function<void()> done;
    };

    JobId Enqueue(std::function<std::function<void()>()> work);
    void Run();

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<Queued> m_queued;
    std::vector<Finished> m_finished;
    /// Jobs being run by a worker.
    std::unordered_set<JobId> m_running;
    /// The subset of m_running whose result is unwanted.
    std::unordered_set<JobId> m_cancelled;
    JobId m_nextId = 1;
    bool m_stopping = false;
    std::vector<std::thread> m_threads;
};

} // namespace daidalos::core
