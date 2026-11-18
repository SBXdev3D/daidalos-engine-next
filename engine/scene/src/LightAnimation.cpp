// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/LightAnimation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace daidalos::scene {

namespace {

constexpr std::array<std::pair<LightPattern, std::string_view>, 6> kNames{{{LightPattern::Steady, "steady"},
                                                                           {LightPattern::Flicker, "flicker"},
                                                                           {LightPattern::Buzz, "buzz"},
                                                                           {LightPattern::Failing, "failing"},
                                                                           {LightPattern::Strobe, "strobe"},
                                                                           {LightPattern::Pulse, "pulse"}}};

// A number from 0 to 1 for an integer, the same each time.
float Hash(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0xFFFFFFU) / static_cast<float>(0x1000000U);
}

// Smooth noise from 0 to 1: random values at whole times, eased between.
float Noise(double time, std::uint32_t seed) {
    const double floor = std::floor(time);
    const auto cell = static_cast<std::int64_t>(floor);
    const float a = Hash(static_cast<std::uint32_t>(cell) * 2654435761U ^ seed);
    const float b = Hash(static_cast<std::uint32_t>(cell + 1) * 2654435761U ^ seed);
    const auto t = static_cast<float>(time - floor);
    const float eased = t * t * (3.0f - 2.0f * t);
    return a + (b - a) * eased;
}

// Whether a random event of a slot of time happens, with a probability, and how far into the slot.
bool Happens(double time, double slot, float probability, std::uint32_t seed, float& into) {
    const auto index = static_cast<std::int64_t>(std::floor(time / slot));
    if (Hash(static_cast<std::uint32_t>(index) * 747796405U ^ seed) >= probability) {
        return false;
    }
    into = static_cast<float>(time / slot - static_cast<double>(index));
    return true;
}

} // namespace

std::string_view LightPatternName(LightPattern pattern) {
    for (const auto& [value, name] : kNames) {
        if (value == pattern) {
            return name;
        }
    }
    return "steady";
}

bool LightPatternOf(std::string_view name, LightPattern& pattern) {
    for (const auto& [value, known] : kNames) {
        if (known == name) {
            pattern = value;
            return true;
        }
    }
    return false;
}

float LightFactor(const LightAnimation& animation, double seconds, std::uint32_t seed) {
    const double time = seconds * std::max(animation.rate, 0.0f);
    float factor = 1.0f;
    switch (animation.pattern) {
    case LightPattern::Steady:
        return 1.0f;
    case LightPattern::Flicker: {
        // Two octaves of noise: the slow sway of a flame and its quick shiver.
        const float slow = Noise(time * 2.0, seed);
        const float fast = Noise(time * 11.0, seed ^ 0x9e3779b9U);
        factor = 0.72f + 0.2f * slow + 0.08f * fast;
        break;
    }
    case LightPattern::Buzz: {
        // Full, with dropouts of a few hundredths of a second; now and then a stutter of them.
        float into = 0.0f;
        if (Happens(time, 0.11, 0.08f, seed, into)) {
            factor = into < 0.5f ? 0.15f : 0.6f;
        } else if (Happens(time, 1.7, 0.12f, seed ^ 0x51ed270bU, into)) {
            factor = std::fmod(into * 9.0f, 1.0f) < 0.5f ? 0.25f : 1.0f;
        } else {
            factor = 0.96f + 0.04f * Noise(time * 60.0, seed);
        }
        break;
    }
    case LightPattern::Failing: {
        // Long spells: mostly dark, then a stutter, then a while on.
        const double spell = 3.0;
        const auto index = static_cast<std::int64_t>(std::floor(time / spell));
        const float kind = Hash(static_cast<std::uint32_t>(index) * 2246822519U ^ seed);
        const auto into = static_cast<float>(time / spell - static_cast<double>(index));
        if (kind < 0.45f) {
            factor = 0.02f; // Dark, a glow left in the filament.
        } else if (kind < 0.8f) {
            // Stutters: on and off at an uneven pace.
            factor = Noise(time * 14.0, seed) > 0.55f ? 0.9f : 0.05f;
        } else {
            factor = into < 0.08f ? 0.3f : 0.85f + 0.1f * Noise(time * 5.0, seed);
        }
        break;
    }
    case LightPattern::Strobe:
        factor = std::fmod(time + Hash(seed) * 0.5, 1.0) < 0.12 ? 1.0f : 0.0f;
        break;
    case LightPattern::Pulse: {
        const double phase = std::fmod(time + Hash(seed), 1.0);
        factor = static_cast<float>(0.5 - 0.5 * std::cos(phase * 2.0 * std::numbers::pi));
        break;
    }
    }
    const float depth = std::clamp(animation.depth, 0.0f, 1.0f);
    return std::clamp(1.0f - depth * (1.0f - factor), 0.0f, 1.0f);
}

void PowerCircuits::Set(std::string_view circuit, bool on, double seconds) {
    auto found = m_states.find(circuit);
    if (found == m_states.end()) {
        found = m_states.emplace(std::string(circuit), State{}).first;
    }
    if (found->second.on != on) {
        found->second = {on, seconds};
    }
}

std::vector<std::pair<std::string, bool>> PowerCircuits::Switched() const {
    std::vector<std::pair<std::string, bool>> switched;
    for (const auto& [name, state] : m_states) {
        switched.emplace_back(name, state.on);
    }
    return switched;
}

bool PowerCircuits::IsOn(std::string_view circuit) const {
    const auto found = m_states.find(circuit);
    return found == m_states.end() || found->second.on;
}

float PowerCircuits::Factor(std::string_view circuit, double seconds) const {
    const auto found = m_states.find(circuit);
    if (found == m_states.end()) {
        return 1.0f;
    }
    const double since = seconds - found->second.since;
    if (!found->second.on) {
        if (since >= kCutSeconds) {
            return 0.0f;
        }
        // A brownout: it sags, catches once, and dies.
        const double t = since / kCutSeconds;
        return static_cast<float>(t < 0.4 ? 0.5 * (1.0 - t) : (t < 0.55 ? 0.7 : 0.25 * (1.0 - t)));
    }
    if (since >= kStartSeconds) {
        return 1.0f;
    }
    // Fluorescent start: dark, two blinks, then on.
    const double t = since / kStartSeconds;
    if (t < 0.25) {
        return 0.0f;
    }
    if (t < 0.32 || (t > 0.5 && t < 0.56)) {
        return 0.8f;
    }
    if (t < 0.7) {
        return 0.05f;
    }
    return 1.0f;
}

} // namespace daidalos::scene
