// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/StartupSequence.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/logging/Log.hpp>

#include <format>
#include <utility>

namespace daidalos::app {

namespace {

constexpr std::string_view kModule = "app";

} // namespace

StartupSequence::~StartupSequence() {
    Stop();
}

void StartupSequence::Add(std::string name, StartFunction start, StopFunction stop) {
    DAI_ASSERT(m_started == 0, "Steps cannot be added to a started sequence");
    DAI_ASSERT(start, "A step needs a start function");
    m_steps.push_back(Step{std::move(name), std::move(start), std::move(stop)});
}

core::Result<void> StartupSequence::Start() {
    DAI_ASSERT(m_started == 0, "The sequence is already started");

    for (const Step& step : m_steps) {
        DAI_LOG_DEBUG(kModule, "Starting {}", step.name);
        if (core::Result<void> result = step.start(); !result) {
            core::Error error = std::move(result.error());
            error.message = std::format("Starting {} failed: {}", step.name, error.message);
            DAI_LOG_CRITICAL(kModule, "{}", core::Format(error));
            Stop();
            return tl::unexpected(std::move(error));
        }
        ++m_started;
    }
    return {};
}

void StartupSequence::Stop() {
    while (m_started > 0) {
        --m_started;
        const Step& step = m_steps[m_started];
        DAI_LOG_DEBUG(kModule, "Stopping {}", step.name);
        if (step.stop) {
            step.stop();
        }
    }
}

} // namespace daidalos::app
