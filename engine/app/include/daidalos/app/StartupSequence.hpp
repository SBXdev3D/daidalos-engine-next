// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace daidalos::app {

/// Ordered startup with rollback. Steps start in the order they were added and stop in reverse order.
/// If a step fails, the steps already started are stopped, in reverse order, before the error is returned,
/// so a failed startup leaves nothing behind.
class StartupSequence {
public:
    using StartFunction = std::function<core::Result<void>()>;
    using StopFunction = std::function<void()>;

    StartupSequence() = default;
    StartupSequence(const StartupSequence&) = delete;
    StartupSequence& operator=(const StartupSequence&) = delete;
    /// Stops the started steps.
    ~StartupSequence();

    /// Adds a step. stop may be empty for a step with nothing to undo. Steps cannot be added once started.
    void Add(std::string name, StartFunction start, StopFunction stop = {});

    /// Starts every step. On failure, returns the step's error with the step name added to its message.
    core::Result<void> Start();

    /// Stops the started steps in reverse order. Does nothing if none is started.
    void Stop();

    [[nodiscard]] std::size_t StartedCount() const noexcept { return m_started; }

private:
    struct Step {
        std::string name;
        StartFunction start;
        StopFunction stop;
    };

    std::vector<Step> m_steps;
    std::size_t m_started = 0;
};

} // namespace daidalos::app
