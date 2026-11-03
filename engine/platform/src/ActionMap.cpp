// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/input/ActionMap.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace daidalos::input {

namespace {

bool ButtonIsDown(const InputState& state, const ActionMap::Button& button) {
    return std::visit(
        [&state](auto control) {
            if constexpr (std::is_same_v<decltype(control), platform::GamepadButton>) {
                return std::ranges::any_of(state.Gamepads(),
                                           [&](platform::GamepadId gamepad) { return state.IsDown(gamepad, control); });
            } else {
                return state.IsDown(control);
            }
        },
        button);
}

bool ButtonWasPressed(const InputState& state, const ActionMap::Button& button) {
    return std::visit(
        [&state](auto control) {
            if constexpr (std::is_same_v<decltype(control), platform::GamepadButton>) {
                return std::ranges::any_of(
                    state.Gamepads(), [&](platform::GamepadId gamepad) { return state.WasPressed(gamepad, control); });
            } else {
                return state.WasPressed(control);
            }
        },
        button);
}

} // namespace

std::vector<ActionMap::Binding>& ActionMap::BindingsOf(Action action) {
    if (action >= m_bindings.size()) {
        m_bindings.resize(action + std::size_t{1});
    }
    return m_bindings[action];
}

void ActionMap::Bind(Action action, Button button) {
    BindingsOf(action).emplace_back(ButtonBinding{button});
}

void ActionMap::BindAxis(Action action, Button positive, Button negative) {
    BindingsOf(action).emplace_back(ButtonPair{positive, negative});
}

void ActionMap::BindAxis(Action action, platform::GamepadAxis axis, float scale, float deadZone) {
    BindingsOf(action).emplace_back(AxisBinding{axis, scale, deadZone});
}

bool ActionMap::IsDown(const InputState& state, Action action) const {
    if (action >= m_bindings.size()) {
        return false;
    }
    return std::ranges::any_of(m_bindings[action], [&state](const Binding& binding) {
        const auto* button = std::get_if<ButtonBinding>(&binding);
        return button != nullptr && ButtonIsDown(state, button->button);
    });
}

bool ActionMap::WasPressed(const InputState& state, Action action) const {
    if (action >= m_bindings.size()) {
        return false;
    }
    return std::ranges::any_of(m_bindings[action], [&state](const Binding& binding) {
        const auto* button = std::get_if<ButtonBinding>(&binding);
        return button != nullptr && ButtonWasPressed(state, button->button);
    });
}

float ActionMap::Value(const InputState& state, Action action) const {
    if (action >= m_bindings.size()) {
        return 0.0f;
    }
    float value = 0.0f;
    for (const Binding& binding : m_bindings[action]) {
        if (const auto* pair = std::get_if<ButtonPair>(&binding)) {
            value += (ButtonIsDown(state, pair->positive) ? 1.0f : 0.0f) -
                     (ButtonIsDown(state, pair->negative) ? 1.0f : 0.0f);
        } else if (const auto* axis = std::get_if<AxisBinding>(&binding)) {
            for (const platform::GamepadId gamepad : state.Gamepads()) {
                const float raw = state.Axis(gamepad, axis->axis);
                if (std::abs(raw) >= axis->deadZone) {
                    value += raw * axis->scale;
                }
            }
        }
    }
    return std::clamp(value, -1.0f, 1.0f);
}

} // namespace daidalos::input
