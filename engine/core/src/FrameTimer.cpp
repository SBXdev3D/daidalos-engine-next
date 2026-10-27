// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/FrameTimer.hpp>

#include <algorithm>

namespace daidalos::core {

FrameTimer::FrameTimer(const FrameTimerConfig& config) : m_config(config) {
    DAI_ASSERT(m_config.fixedStep > Seconds::zero(), "The fixed step must be positive");
    DAI_ASSERT(m_config.maxDelta >= m_config.fixedStep, "The maximum delta must hold at least one fixed step");
}

FrameTime FrameTimer::Advance(Seconds elapsed) {
    FrameTime time;
    time.frameIndex = m_frameIndex++;
    time.unclampedDelta = elapsed;
    time.delta = std::clamp(elapsed, Seconds::zero(), m_config.maxDelta);

    m_total += time.delta;
    time.total = m_total;

    m_accumulator += time.delta;
    while (m_accumulator >= m_config.fixedStep) {
        m_accumulator -= m_config.fixedStep;
        ++time.fixedSteps;
    }
    time.alpha = m_accumulator / m_config.fixedStep;
    return time;
}

} // namespace daidalos::core
