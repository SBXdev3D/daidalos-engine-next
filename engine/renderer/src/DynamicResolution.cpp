// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "DynamicResolution.hpp"

#include <algorithm>
#include <cmath>

namespace daidalos::renderer {

float DynamicResolution::Update(std::optional<double> gpuTime, float seconds,
                                const DynamicResolutionSettings& settings) {
    const float highest = std::clamp(settings.maxScale, 0.25f, 1.0f);
    const float lowest = std::clamp(settings.minScale, 0.25f, highest);
    const float previous = m_scale;
    if (!settings.enabled) {
        m_scale = highest;
        m_underSeconds = 0.0f;
    } else if (m_settling > 0) {
        // Timed before the last change.
        --m_settling;
    } else if (gpuTime.has_value() && *gpuTime > 0.0 && settings.budget > 0.0) {
        if (*gpuTime > settings.budget) {
            // The cost follows the pixels, the square of the scale.
            m_scale *= static_cast<float>(std::sqrt(settings.budget * kMargin / *gpuTime));
            m_underSeconds = 0.0f;
        } else if (*gpuTime < settings.budget * kRaiseBelow) {
            m_underSeconds += seconds;
            if (m_underSeconds >= kRaiseSeconds) {
                m_scale += kStep;
                m_underSeconds = 0.0f;
            }
        } else {
            m_underSeconds = 0.0f;
        }
    }
    m_scale = std::clamp(m_scale, lowest, highest);
    if (m_scale != previous) {
        m_settling = m_latency;
    }
    return m_scale;
}

rhi::Extent RenderExtent(rhi::Extent output, float scale) {
    const auto scaled = [scale](std::uint32_t size) {
        return std::clamp(static_cast<std::uint32_t>(std::lround(static_cast<float>(size) * scale)), 1U,
                          std::max(size, 1U));
    };
    return {scaled(output.width), scaled(output.height)};
}

} // namespace daidalos::renderer
