// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/input/Rumble.hpp>

#include <algorithm>
#include <cmath>

namespace daidalos::input {

namespace {

// Changes smaller than this are not felt, and not worth a message to the pad.
constexpr float kFelt = 0.04f;

} // namespace

void RumbleMixer::Pulse(float low, float high, float seconds) {
    if (seconds <= 0.0f || (low <= 0.0f && high <= 0.0f)) {
        return;
    }
    m_pulses.push_back({.start = {std::clamp(low, 0.0f, 1.0f), std::clamp(high, 0.0f, 1.0f)}, .seconds = seconds});
}

void RumbleMixer::Stop() {
    m_pulses.clear();
    m_hold = {};
}

std::optional<RumbleLevel> RumbleMixer::Update(float seconds) {
    RumbleLevel level = m_hold;
    for (Shake& shake : m_pulses) {
        shake.age += seconds;
        const float left = std::max(1.0f - (shake.age / shake.seconds), 0.0f);
        level.low += shake.start.low * left;
        level.high += shake.start.high * left;
    }
    std::erase_if(m_pulses, [](const Shake& shake) { return shake.age >= shake.seconds; });
    level.low = std::clamp(level.low, 0.0f, 1.0f);
    level.high = std::clamp(level.high, 0.0f, 1.0f);
    m_current = level;

    m_sinceSent += seconds;
    const bool changed = std::abs(level.low - m_sent.low) >= kFelt || std::abs(level.high - m_sent.high) >= kFelt;
    // Stillness is sent exactly, so that a pad never keeps a faint hum.
    const bool stopped = level.IsStill() && !m_sent.IsStill();
    // Sent again at half its time, so that a held level never lapses between two messages.
    const bool lapsing = !level.IsStill() && m_sinceSent >= kSentSeconds * 0.5f;
    if (!changed && !stopped && !lapsing) {
        return std::nullopt;
    }
    m_sent = level;
    m_sinceSent = 0.0f;
    return level;
}

} // namespace daidalos::input
