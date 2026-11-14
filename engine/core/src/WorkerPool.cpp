// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/WorkerPool.hpp>

#include <algorithm>

namespace daidalos::core {

WorkerPool::WorkerPool(std::uint32_t threadCount) {
    const std::uint32_t count = std::max<std::uint32_t>(threadCount, 1);
    m_threads.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        m_threads.emplace_back([this] { Run(); });
    }
}

WorkerPool::~WorkerPool() {
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
        m_queued.clear();
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads) {
        thread.join();
    }
}

JobId WorkerPool::Enqueue(std::function<std::function<void()>()> work) {
    JobId id = 0;
    {
        const std::scoped_lock lock(m_mutex);
        id = m_nextId++;
        m_queued.push_back({id, std::move(work)});
    }
    m_wake.notify_one();
    return id;
}

void WorkerPool::Run() {
    std::unique_lock lock(m_mutex);
    while (true) {
        m_wake.wait(lock, [this] { return m_stopping || !m_queued.empty(); });
        if (m_stopping) {
            return;
        }
        Queued job = std::move(m_queued.front());
        m_queued.pop_front();
        m_running.insert(job.id);

        lock.unlock();
        std::function<void()> done = job.work();
        job.work = nullptr; // What it captured is released outside the lock.
        lock.lock();

        m_running.erase(job.id);
        if (m_cancelled.erase(job.id) == 0) {
            m_finished.push_back({job.id, std::move(done)});
        }
        m_idle.notify_all();
    }
}

void WorkerPool::Cancel(JobId job) {
    const std::scoped_lock lock(m_mutex);
    if (std::erase_if(m_queued, [job](const Queued& queued) { return queued.id == job; }) > 0) {
        m_idle.notify_all();
        return;
    }
    if (std::erase_if(m_finished, [job](const Finished& finished) { return finished.id == job; }) > 0) {
        return;
    }
    if (m_running.contains(job)) {
        m_cancelled.insert(job);
    }
}

void WorkerPool::Deliver() {
    std::vector<Finished> finished;
    {
        const std::scoped_lock lock(m_mutex);
        finished.swap(m_finished);
    }
    // Without the lock, because a done function may submit or cancel jobs.
    for (Finished& job : finished) {
        job.done();
    }
}

std::size_t WorkerPool::PendingCount() const {
    const std::scoped_lock lock(m_mutex);
    return m_queued.size() + m_running.size() + m_finished.size();
}

void WorkerPool::Finish() {
    {
        std::unique_lock lock(m_mutex);
        m_idle.wait(lock, [this] { return m_queued.empty() && m_running.empty(); });
    }
    Deliver();
}

} // namespace daidalos::core
