// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/vec2.hpp>

namespace sandbox {

/// The player's flashlight beyond its light (ADR-0058): how its beam lags behind the camera, its battery, and
/// a failure that scripted scares set off.
struct FlashlightState {
    /// Radians the beam is turned from where the camera looks, around up and around right.
    glm::vec2 lag{0.0f};
    glm::vec2 velocity{0.0f};
    /// The phase of the walking bob, in radians, and how much of it shows, from 0 standing to 1 at a walk.
    float bob = 0.0f;
    float stride = 0.0f;
    /// From 1, full, to 0, empty.
    float battery = 1.0f;
    /// Seconds left of a failure: the beam stutters and dies at random until then.
    float fault = 0.0f;
};

/// Moves the beam's lag for one frame: the camera turned by these radians, around up (yaw) and around right
/// (pitch), and the player walks at this speed in meters per second. The beam is held in a hand: it trails
/// behind a turn, catches up like a spring, and sways with each step.
void SwayFlashlight(FlashlightState& state, float yawTurned, float pitchTurned, float walkSpeed, float seconds);

/// The beam's turn from the camera's aim this frame, the bob included: around up, then around right.
[[nodiscard]] glm::vec2 FlashlightAim(const FlashlightState& state);

/// Drains the battery while the light is on. lifetime is the seconds a full battery lasts; 0 never drains.
void DrainFlashlight(FlashlightState& state, float seconds, float lifetime);

/// The beam's brightness, from 0 to 1, at a time: full on a good battery, buzzing below 15%, failing while a
/// fault lasts, dark when the battery is empty.
[[nodiscard]] float FlashlightBrightness(const FlashlightState& state, double time);

/// Below this charge, the beam buzzes and dims.
inline constexpr float kLowBattery = 0.15f;

} // namespace sandbox
