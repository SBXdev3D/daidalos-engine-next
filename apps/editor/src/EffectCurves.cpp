// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The effect panel's curve arithmetic, apart from its interface so that it can be tested (ADR-0114).

#include <algorithm>

#include "EffectPanel.hpp"

namespace editor {

std::vector<std::pair<float, float>> SortedKeys(std::vector<std::pair<float, float>> keys) {
    for (auto& [time, value] : keys) {
        time = std::clamp(time, 0.0f, 1.0f);
    }
    std::ranges::stable_sort(keys, {}, &std::pair<float, float>::first);
    return keys;
}

float CurveAt(const std::vector<std::pair<float, float>>& keys, float time) {
    if (keys.empty()) {
        return 0.0f;
    }
    if (time <= keys.front().first) {
        return keys.front().second;
    }
    for (std::size_t index = 1; index < keys.size(); ++index) {
        if (time <= keys[index].first) {
            const auto& [t0, v0] = keys[index - 1];
            const auto& [t1, v1] = keys[index];
            return t1 > t0 ? v0 + (v1 - v0) * (time - t0) / (t1 - t0) : v1;
        }
    }
    return keys.back().second;
}

} // namespace editor
