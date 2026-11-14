// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/FrameTimer.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::ecs {

class World;

/// Runs systems in the order they were added (ADR-0006). There is no automatic dependency
/// ordering: the order is written once, where the schedule is built, so it can be read in one place.
///
///   schedule.Add("Movement", MoveEntities);
///   schedule.Add("Transforms", [](World& world, core::Seconds) { PropagateTransforms(world); });
///   schedule.Run(world, delta);
class Schedule {
public:
    using SystemFunction = std::function<void(World& world, core::Seconds delta)>;

    /// Appends a system. Names must be unique; they appear in the profiler.
    void Add(std::string name, SystemFunction function);

    /// Runs every system once, in order.
    void Run(World& world, core::Seconds delta) const;

    [[nodiscard]] std::size_t Size() const { return m_systems.size(); }
    [[nodiscard]] std::vector<std::string_view> Names() const;

private:
    struct System {
        std::string name;
        SystemFunction function;
    };

    std::vector<System> m_systems;
};

} // namespace daidalos::ecs
