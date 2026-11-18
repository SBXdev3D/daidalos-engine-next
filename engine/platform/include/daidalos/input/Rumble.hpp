// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <optional>
#include <vector>

namespace daidalos::input {

/// How hard a gamepad shakes, from 0 to 1, on its two motors: the heavy low one and the light high one.
struct RumbleLevel {
    float low = 0.0f;
    float high = 0.0f;
    [[nodiscard]] bool IsStill() const { return low <= 0.0f && high <= 0.0f; }
};

/// What a game asks of the gamepads' motors, mixed into one level (ADR-0076): pulses that fade, for a shot or
/// a blow, over a steady level the game holds, such as a heartbeat. Update says when the level is worth
/// sending, because a gamepad takes each change as a message and frames come faster than it needs them.
class RumbleMixer {
public:
    /// Seconds a level sent lasts on the pad: Update sends it again before it runs out.
    static constexpr float kSentSeconds = 1.0f;

    /// A shake that starts at this level and fades to nothing over the seconds. Pulses add up.
    void Pulse(float low, float high, float seconds);
    /// A level held until changed, under the pulses.
    void Hold(RumbleLevel level) { m_hold = level; }
    /// Stops everything, as when the game pauses.
    void Stop();

    /// Moves time on. Returns the level to send to the pads now, or nothing while what they have will do: a
    /// change of a few hundredths, or the same level with time left on it.
    [[nodiscard]] std::optional<RumbleLevel> Update(float seconds);
    [[nodiscard]] RumbleLevel Current() const { return m_current; }

private:
    struct Shake {
        RumbleLevel start;
        float seconds = 0.0f;
        float age = 0.0f;
    };

    std::vector<Shake> m_pulses;
    RumbleLevel m_hold;
    RumbleLevel m_current;
    RumbleLevel m_sent;
    float m_sinceSent = 0.0f;
};

} // namespace daidalos::input
