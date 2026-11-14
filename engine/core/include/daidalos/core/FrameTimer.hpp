// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>

namespace daidalos::core {

/// Durations in the engine are seconds stored as double (CONVENTIONS.md, section 5).
using Seconds = std::chrono::duration<double>;

struct FrameTimerConfig {
    /// Longer frames (breakpoints, window drags, loading hitches) are clamped to this duration,
    /// so that the simulation does not try to catch up with one huge step.
    Seconds maxDelta{0.25};
    /// Duration of one fixed simulation step. Physics runs at 60 Hz (CONVENTIONS.md, section 7).
    Seconds fixedStep{1.0 / 60.0};
};

/// Timing of one frame, as computed by FrameTimer::Advance.
struct FrameTime {
    /// 0 for the first frame.
    std::uint64_t frameIndex = 0;
    /// Elapsed time used for this frame, after clamping.
    Seconds delta{};
    /// Elapsed time as measured, before clamping.
    Seconds unclampedDelta{};
    /// Sum of the clamped deltas since the timer was created.
    Seconds total{};
    /// Number of fixed steps to run this frame.
    std::uint32_t fixedSteps = 0;
    /// Fraction of a fixed step left in the accumulator, in [0, 1). Used to interpolate
    /// between the last two fixed-step states when rendering.
    double alpha = 0.0;
};

/// Turns the measured duration of each frame into a variable delta and a number of fixed steps.
/// It does not read a clock: the caller passes the elapsed time, which keeps it deterministic.
class FrameTimer {
public:
    explicit FrameTimer(const FrameTimerConfig& config = {});

    /// Advances by the time elapsed since the previous call. A negative duration counts as zero.
    FrameTime Advance(Seconds elapsed);

    [[nodiscard]] const FrameTimerConfig& Config() const noexcept { return m_config; }

private:
    FrameTimerConfig m_config;
    std::uint64_t m_frameIndex = 0;
    Seconds m_total{};
    Seconds m_accumulator{};
};

} // namespace daidalos::core
