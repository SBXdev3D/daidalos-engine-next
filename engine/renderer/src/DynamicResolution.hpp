// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/rhi/Types.hpp>

#include <cstdint>
#include <optional>

namespace daidalos::renderer {

/// The render scale's bounds and the GPU's budget (ADR-0049).
struct DynamicResolutionSettings {
    /// Moves the scale to hold the budget; otherwise it stays at maxScale.
    bool enabled = true;
    /// The scale, render size over output size per axis, at most and at least.
    float maxScale = 1.0f;
    float minScale = 0.5f;
    /// Milliseconds of GPU time a frame may take.
    double budget = 15.0;
};

/// The render scale that holds a GPU budget, from the GPU times of past frames: down at once to what the
/// budget allows, assuming the cost follows the pixels; up by a step after the frames have stayed well under
/// the budget for a while, so that it does not hunt (ADR-0049). On the CPU, as the timings come back a few
/// frames late, so that its rules are tested there.
class DynamicResolution {
public:
    /// Up when the frames take less than this share of the budget.
    static constexpr double kRaiseBelow = 0.85;
    /// For this long.
    static constexpr float kRaiseSeconds = 0.5f;
    /// By this much of the scale.
    static constexpr float kStep = 0.025f;
    /// Aims this far under the budget when going down.
    static constexpr double kMargin = 0.95;

    /// `latency` frames go by before a frame's GPU time comes back: as many after a change are still timed
    /// at the old scale, and are passed over.
    explicit DynamicResolution(std::uint32_t latency) : m_latency(latency) {}

    /// The scale for the next frame, from the GPU time of the last one that came back, in milliseconds, if
    /// any, and the seconds since the last update.
    float Update(std::optional<double> gpuTime, float seconds, const DynamicResolutionSettings& settings);

    [[nodiscard]] float Scale() const { return m_scale; }

private:
    float m_scale = 1.0f;
    float m_underSeconds = 0.0f;
    std::uint32_t m_settling = 0;
    std::uint32_t m_latency;
};

/// The render size at a scale of the output's, at least a pixel each way.
[[nodiscard]] rhi::Extent RenderExtent(rhi::Extent output, float scale);

} // namespace daidalos::renderer
