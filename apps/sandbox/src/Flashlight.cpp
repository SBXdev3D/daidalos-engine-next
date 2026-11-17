// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Flashlight.hpp"

#include <daidalos/scene/LightAnimation.hpp>

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

using namespace daidalos;

namespace sandbox {

namespace {

constexpr float kStiffness = 90.0f; // The spring that brings the beam back to the aim, per second squared.
constexpr float kMaxLag = 0.14f;    // Radians: the hand never trails further.
constexpr float kStepLength = 1.4f; // Meters per full cycle of the bob, two steps.
constexpr float kBobYaw = 0.006f;   // Radians the beam sways sideways, at a walk.
constexpr float kBobPitch = 0.010f; // And up and down.
constexpr float kWalkSpeed = 4.0f;  // Meters per second at which the bob is full.
constexpr std::uint32_t kSeed = 0x5eed;

} // namespace

void SwayFlashlight(FlashlightState& state, float yawTurned, float pitchTurned, float walkSpeed, float seconds) {
    if (seconds <= 0.0f) {
        return;
    }
    // The camera turned; the hand has not yet.
    state.lag -= glm::vec2(yawTurned, pitchTurned);
    state.lag = glm::clamp(state.lag, glm::vec2(-kMaxLag), glm::vec2(kMaxLag));
    // A critically damped spring brings it back, in steps short enough to stay stable.
    const float damping = 2.0f * std::sqrt(kStiffness);
    const int steps = std::max(1, static_cast<int>(std::ceil(seconds / 0.005f)));
    const float step = seconds / static_cast<float>(steps);
    for (int index = 0; index < steps; ++index) {
        state.velocity += (-kStiffness * state.lag - damping * state.velocity) * step;
        state.lag += state.velocity * step;
    }
    state.bob = std::fmod(state.bob + walkSpeed / kStepLength * 6.2831853f * seconds, 62.831853f);
    // The sway eases in and out with the walk, so stopping leaves the beam where it aims.
    const float target = std::clamp(walkSpeed / kWalkSpeed, 0.0f, 1.0f);
    state.stride += (target - state.stride) * std::min(seconds * 6.0f, 1.0f);
}

glm::vec2 FlashlightAim(const FlashlightState& state) {
    // The bob grows with the walk; at rest it settles where it was.
    return state.lag + state.stride * glm::vec2(kBobYaw * std::sin(state.bob * 0.5f), kBobPitch * std::sin(state.bob));
}

void DrainFlashlight(FlashlightState& state, float seconds, float lifetime) {
    if (lifetime > 0.0f) {
        state.battery = std::max(state.battery - seconds / lifetime, 0.0f);
    }
    state.fault = std::max(state.fault - seconds, 0.0f);
}

float FlashlightBrightness(const FlashlightState& state, double time) {
    if (state.battery <= 0.0f) {
        return 0.0f;
    }
    float brightness = 1.0f;
    if (state.battery < kLowBattery) {
        // A weak battery: dimmer as it goes, and buzzing.
        const float weak = state.battery / kLowBattery;
        brightness =
            (0.35f + 0.65f * weak) *
            scene::LightFactor({.pattern = scene::LightPattern::Buzz, .rate = 1.5f, .depth = 1.0f - weak}, time, kSeed);
    }
    if (state.fault > 0.0f) {
        brightness *= scene::LightFactor({.pattern = scene::LightPattern::Failing, .rate = 3.0f}, time, kSeed);
    }
    return std::clamp(brightness, 0.0f, 1.0f);
}

} // namespace sandbox
