// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/input/InputState.hpp>

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace daidalos::input {

/// Actions are numbers chosen by the game, usually an enum: Fire, Jump, Strafe.
using Action = std::uint32_t;

/// Maps physical controls to what they mean in the game (OVERVIEW §12.2), so that gameplay code reads
/// actions and does not care about keyboards or gamepads. Every connected gamepad counts.
///
///   actions.Bind(Fire, platform::MouseButton::Left);
///   actions.Bind(Fire, platform::GamepadButton::RightShoulder);
///   actions.BindAxis(Strafe, platform::Key::D, platform::Key::A);
///   actions.BindAxis(Strafe, platform::GamepadAxis::LeftX);
class ActionMap {
public:
    using Button = std::variant<platform::Key, platform::MouseButton, platform::GamepadButton>;

    /// Gamepad axes read 0 below this value, because sticks rest slightly off center.
    static constexpr float kDefaultDeadZone = 0.15f;

    /// The control also triggers the action.
    void Bind(Action action, Button button);
    /// The two controls move the axis to +1 and -1. Holding both gives 0.
    void BindAxis(Action action, Button positive, Button negative);
    /// The gamepad's axis moves this one, multiplied by the scale.
    void BindAxis(Action action, platform::GamepadAxis axis, float scale = 1.0f, float deadZone = kDefaultDeadZone);

    /// Held on any of the controls bound to the action.
    [[nodiscard]] bool IsDown(const InputState& state, Action action) const;
    /// Pressed this frame on any of those controls.
    [[nodiscard]] bool WasPressed(const InputState& state, Action action) const;
    /// Sum of everything bound to the axis, between -1 and +1.
    [[nodiscard]] float Value(const InputState& state, Action action) const;

private:
    struct ButtonBinding {
        Button button;
    };
    struct ButtonPair {
        Button positive;
        Button negative;
    };
    struct AxisBinding {
        platform::GamepadAxis axis;
        float scale;
        float deadZone;
    };
    using Binding = std::variant<ButtonBinding, ButtonPair, AxisBinding>;

    std::vector<Binding>& BindingsOf(Action action);

    std::vector<std::vector<Binding>> m_bindings;
};

} // namespace daidalos::input
