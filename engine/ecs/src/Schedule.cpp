// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Schedule.hpp>

#include <algorithm>
#include <utility>

namespace daidalos::ecs {

void Schedule::Add(std::string name, SystemFunction function) {
    DAI_ASSERT(function, "A system needs a function");
    DAI_ASSERT(std::ranges::none_of(m_systems, [&](const System& system) { return system.name == name; }),
               "System names must be unique");
    m_systems.push_back(System{std::move(name), std::move(function)});
}

void Schedule::Run(World& world, core::Seconds delta) const {
    for (const System& system : m_systems) {
        DAI_PROFILE_SCOPE_TEXT(system.name);
        system.function(world, delta);
    }
}

std::vector<std::string_view> Schedule::Names() const {
    std::vector<std::string_view> names;
    names.reserve(m_systems.size());
    for (const System& system : m_systems) {
        names.emplace_back(system.name);
    }
    return names;
}

} // namespace daidalos::ecs
