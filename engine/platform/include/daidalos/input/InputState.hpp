// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/platform/Event.hpp>
#include <daidalos/platform/InputCodes.hpp>

#include <glm/vec2.hpp>

#include <array>
#include <bitset>
#include <cstddef>
#include <vector>

namespace daidalos::input {

/// Keyboard, mouse and gamepad state, built from platform events (OVERVIEW §12.2). It does not call
/// the platform, so it can be driven by recorded or synthetic events.
///
/// Each frame: BeginFrame(), then Process() for every event of the frame, then queries.
/// - IsDown: held now.
/// - WasPressed / WasReleased: changed during this frame. A press and a release in the same frame
///   both count, so short taps are never lost.
/// Key repeats do not count as presses. When a window loses focus, every held key and button is
/// released and the axes return to zero, because the release events would never arrive.
class InputState {
public:
    using Key = platform::Key;
    using MouseButton = platform::MouseButton;
    using GamepadId = platform::GamepadId;
    using GamepadButton = platform::GamepadButton;
    using GamepadAxis = platform::GamepadAxis;

    /// Clears what happened during the previous frame: presses, releases, mouse motion and scrolling.
    void BeginFrame();
    void Process(const platform::Event& event);

    [[nodiscard]] bool IsDown(Key key) const;
    [[nodiscard]] bool WasPressed(Key key) const;
    [[nodiscard]] bool WasReleased(Key key) const;

    [[nodiscard]] bool IsDown(MouseButton button) const;
    [[nodiscard]] bool WasPressed(MouseButton button) const;
    [[nodiscard]] bool WasReleased(MouseButton button) const;
    /// Last known pointer position, in window coordinates (see platform::MouseMoved).
    [[nodiscard]] glm::vec2 MousePosition() const { return m_mousePosition; }
    /// Pointer motion during this frame.
    [[nodiscard]] glm::vec2 MouseDelta() const { return m_mouseDelta; }
    /// Scrolling during this frame, in notches.
    [[nodiscard]] glm::vec2 WheelDelta() const { return m_wheelDelta; }

    /// Connected gamepads, in connection order.
    [[nodiscard]] std::vector<GamepadId> Gamepads() const;
    [[nodiscard]] bool IsConnected(GamepadId gamepad) const;
    /// False for gamepads that are not connected.
    [[nodiscard]] bool IsDown(GamepadId gamepad, GamepadButton button) const;
    [[nodiscard]] bool WasPressed(GamepadId gamepad, GamepadButton button) const;
    [[nodiscard]] bool WasReleased(GamepadId gamepad, GamepadButton button) const;
    /// Raw axis value, without dead zone; 0 for gamepads that are not connected.
    [[nodiscard]] float Axis(GamepadId gamepad, GamepadAxis axis) const;

private:
    template <std::size_t Count>
    struct Buttons {
        std::bitset<Count> down;
        std::bitset<Count> pressed;
        std::bitset<Count> released;

        void Press(std::size_t index);
        void Release(std::size_t index);
        void ReleaseAll();
        void ClearEdges();
    };

    struct Gamepad {
        GamepadId id{};
        Buttons<platform::kGamepadButtonCount> buttons;
        std::array<float, platform::kGamepadAxisCount> axes{};
    };

    [[nodiscard]] Gamepad* FindGamepad(GamepadId id);
    [[nodiscard]] const Gamepad* FindGamepad(GamepadId id) const;
    void ReleaseEverything();

    Buttons<platform::kKeyCount> m_keys;
    Buttons<platform::kMouseButtonCount> m_mouseButtons;
    glm::vec2 m_mousePosition{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    glm::vec2 m_wheelDelta{0.0f};
    std::vector<Gamepad> m_gamepads;
};

} // namespace daidalos::input
