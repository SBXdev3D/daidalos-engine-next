// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/input/InputState.hpp>

#include <algorithm>
#include <variant>

namespace daidalos::input {

namespace {

template <class... Handlers>
struct Overloaded : Handlers... {
    using Handlers::operator()...;
};

std::size_t IndexOf(platform::Key key) {
    return static_cast<std::size_t>(key);
}

std::size_t IndexOf(platform::MouseButton button) {
    return static_cast<std::size_t>(button);
}

std::size_t IndexOf(platform::GamepadButton button) {
    return static_cast<std::size_t>(button);
}

} // namespace

template <std::size_t Count>
void InputState::Buttons<Count>::Press(std::size_t index) {
    if (index < Count && !down[index]) {
        down[index] = true;
        pressed[index] = true;
    }
}

template <std::size_t Count>
void InputState::Buttons<Count>::Release(std::size_t index) {
    if (index < Count && down[index]) {
        down[index] = false;
        released[index] = true;
    }
}

template <std::size_t Count>
void InputState::Buttons<Count>::ReleaseAll() {
    released |= down;
    down.reset();
}

template <std::size_t Count>
void InputState::Buttons<Count>::ClearEdges() {
    pressed.reset();
    released.reset();
}

void InputState::BeginFrame() {
    m_keys.ClearEdges();
    m_mouseButtons.ClearEdges();
    for (Gamepad& gamepad : m_gamepads) {
        gamepad.buttons.ClearEdges();
    }
    m_mouseDelta = glm::vec2(0.0f);
    m_wheelDelta = glm::vec2(0.0f);
}

void InputState::Process(const platform::Event& event) {
    std::visit(
        Overloaded{
            [this](const platform::KeyPressed& pressed) {
                if (!pressed.repeat) {
                    m_keys.Press(IndexOf(pressed.key));
                }
            },
            [this](const platform::KeyReleased& released) { m_keys.Release(IndexOf(released.key)); },
            [this](const platform::MouseMoved& moved) {
                m_mousePosition = moved.position;
                m_mouseDelta += moved.delta;
            },
            [this](const platform::MouseButtonPressed& pressed) {
                m_mousePosition = pressed.position;
                m_mouseButtons.Press(IndexOf(pressed.button));
            },
            [this](const platform::MouseButtonReleased& released) {
                m_mousePosition = released.position;
                m_mouseButtons.Release(IndexOf(released.button));
            },
            [this](const platform::MouseWheelScrolled& scrolled) { m_wheelDelta += scrolled.delta; },
            [this](const platform::GamepadConnected& connected) {
                if (FindGamepad(connected.gamepad) == nullptr) {
                    m_gamepads.push_back(Gamepad{connected.gamepad, {}, {}});
                }
            },
            [this](const platform::GamepadDisconnected& disconnected) {
                std::erase_if(m_gamepads, [&](const Gamepad& gamepad) { return gamepad.id == disconnected.gamepad; });
            },
            [this](const platform::GamepadButtonPressed& pressed) {
                if (Gamepad* gamepad = FindGamepad(pressed.gamepad)) {
                    gamepad->buttons.Press(IndexOf(pressed.button));
                }
            },
            [this](const platform::GamepadButtonReleased& released) {
                if (Gamepad* gamepad = FindGamepad(released.gamepad)) {
                    gamepad->buttons.Release(IndexOf(released.button));
                }
            },
            [this](const platform::GamepadAxisMoved& moved) {
                const auto axis = static_cast<std::size_t>(moved.axis);
                if (Gamepad* gamepad = FindGamepad(moved.gamepad); gamepad != nullptr && axis < gamepad->axes.size()) {
                    gamepad->axes[axis] = moved.value;
                }
            },
            [this](const platform::WindowFocusLost&) { ReleaseEverything(); },
            [](const auto&) {},
        },
        event);
}

void InputState::ReleaseEverything() {
    m_keys.ReleaseAll();
    m_mouseButtons.ReleaseAll();
    for (Gamepad& gamepad : m_gamepads) {
        gamepad.buttons.ReleaseAll();
        gamepad.axes.fill(0.0f);
    }
}

bool InputState::IsDown(Key key) const {
    return IndexOf(key) < platform::kKeyCount && m_keys.down[IndexOf(key)];
}

bool InputState::WasPressed(Key key) const {
    return IndexOf(key) < platform::kKeyCount && m_keys.pressed[IndexOf(key)];
}

bool InputState::WasReleased(Key key) const {
    return IndexOf(key) < platform::kKeyCount && m_keys.released[IndexOf(key)];
}

bool InputState::IsDown(MouseButton button) const {
    return IndexOf(button) < platform::kMouseButtonCount && m_mouseButtons.down[IndexOf(button)];
}

bool InputState::WasPressed(MouseButton button) const {
    return IndexOf(button) < platform::kMouseButtonCount && m_mouseButtons.pressed[IndexOf(button)];
}

bool InputState::WasReleased(MouseButton button) const {
    return IndexOf(button) < platform::kMouseButtonCount && m_mouseButtons.released[IndexOf(button)];
}

std::vector<InputState::GamepadId> InputState::Gamepads() const {
    std::vector<GamepadId> ids;
    ids.reserve(m_gamepads.size());
    for (const Gamepad& gamepad : m_gamepads) {
        ids.push_back(gamepad.id);
    }
    return ids;
}

bool InputState::IsConnected(GamepadId gamepad) const {
    return FindGamepad(gamepad) != nullptr;
}

bool InputState::IsDown(GamepadId gamepad, GamepadButton button) const {
    const Gamepad* state = FindGamepad(gamepad);
    return state != nullptr && IndexOf(button) < platform::kGamepadButtonCount && state->buttons.down[IndexOf(button)];
}

bool InputState::WasPressed(GamepadId gamepad, GamepadButton button) const {
    const Gamepad* state = FindGamepad(gamepad);
    return state != nullptr && IndexOf(button) < platform::kGamepadButtonCount &&
           state->buttons.pressed[IndexOf(button)];
}

bool InputState::WasReleased(GamepadId gamepad, GamepadButton button) const {
    const Gamepad* state = FindGamepad(gamepad);
    return state != nullptr && IndexOf(button) < platform::kGamepadButtonCount &&
           state->buttons.released[IndexOf(button)];
}

float InputState::Axis(GamepadId gamepad, GamepadAxis axis) const {
    const Gamepad* state = FindGamepad(gamepad);
    const auto index = static_cast<std::size_t>(axis);
    return (state != nullptr && index < state->axes.size()) ? state->axes[index] : 0.0f;
}

InputState::Gamepad* InputState::FindGamepad(GamepadId id) {
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    return found != m_gamepads.end() ? &*found : nullptr;
}

const InputState::Gamepad* InputState::FindGamepad(GamepadId id) const {
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    return found != m_gamepads.end() ? &*found : nullptr;
}

} // namespace daidalos::input
