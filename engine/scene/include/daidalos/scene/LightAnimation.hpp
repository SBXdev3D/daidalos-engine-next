// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace daidalos::scene {

/// How a light's brightness changes over time (ADR-0057). One factor drives the light and the emissive mesh
/// of its lamp together.
enum class LightPattern : std::uint8_t {
    /// Always at full brightness.
    Steady,
    /// A flame: candles, lighters, fire. Smooth noise around a little under full.
    Flicker,
    /// A tired fluorescent tube: full, with short dropouts now and then.
    Buzz,
    /// A light about to die: long dark spells, stutters back on, holds a while.
    Failing,
    /// On and off at the rate, as a hazard beacon.
    Strobe,
    /// Swells and fades at the rate, as an emergency light.
    Pulse,
};

/// What makes a light's brightness change: a pattern, its speed and how deep it goes, and the power circuit
/// it is on. Part of scene::Light, written in level files.
struct LightAnimation {
    LightPattern pattern = LightPattern::Steady;
    /// Times faster than the pattern's own pace; for Strobe and Pulse, cycles per second.
    float rate = 1.0f;
    /// 0: no change at all; 1: the pattern's whole range, down to dark.
    float depth = 1.0f;
    /// The power circuit the light is on, which game code can cut (PowerCircuits); empty for none.
    std::string circuit;
    [[nodiscard]] bool operator==(const LightAnimation&) const = default;
    [[nodiscard]] bool Animated() const { return pattern != LightPattern::Steady || !circuit.empty(); }
};

/// The name of a pattern in level files, and the pattern of a name.
[[nodiscard]] std::string_view LightPatternName(LightPattern pattern);
[[nodiscard]] bool LightPatternOf(std::string_view name, LightPattern& pattern);

/// The brightness a pattern gives at a time, from 0 to 1. seed sets it apart from other lights with the same
/// pattern, so that two candles do not flicker together. Deterministic: the same arguments give the same
/// factor.
[[nodiscard]] float LightFactor(const LightAnimation& animation, double seconds, std::uint32_t seed);

/// The power circuits of a scene, which lights name (LightAnimation::circuit). A circuit that is cut stutters
/// out over a fraction of a second, and one that comes back blinks on as fluorescent tubes start. Circuits
/// no one switched are on.
class PowerCircuits {
public:
    void Set(std::string_view circuit, bool on, double seconds);
    [[nodiscard]] bool IsOn(std::string_view circuit) const;
    /// The brightness the circuit allows at a time, from 0 to 1.
    [[nodiscard]] float Factor(std::string_view circuit, double seconds) const;

    /// Seconds a cut takes to go dark, and a return to come fully on.
    static constexpr double kCutSeconds = 0.35;
    static constexpr double kStartSeconds = 0.9;

private:
    struct State {
        bool on = true;
        double since = -1e9;
    };
    std::map<std::string, State, std::less<>> m_states;
};

} // namespace daidalos::scene
