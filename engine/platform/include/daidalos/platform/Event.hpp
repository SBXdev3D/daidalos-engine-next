// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/platform/InputCodes.hpp>

#include <glm/vec2.hpp>

#include <cstdint>
#include <variant>

namespace daidalos::platform {

/// Identifies a window in events. Never 0 for a live window.
enum class WindowId : std::uint32_t {};

/// Size of a window or surface.
struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent&) const = default;
};

/// The user or the system asked the application to quit (last window closed, Ctrl+C, logout).
struct QuitRequested {
    bool operator==(const QuitRequested&) const = default;
};

/// The user clicked the close button of a window. The window stays open until it is destroyed.
struct WindowCloseRequested {
    WindowId window{};
    bool operator==(const WindowCloseRequested&) const = default;
};

/// The drawable size of a window changed. The size is in pixels, which is what a swapchain needs;
/// it differs from the window size on high-density displays.
struct WindowResized {
    WindowId window{};
    Extent pixelSize;
    bool operator==(const WindowResized&) const = default;
};

struct WindowMinimized {
    WindowId window{};
    bool operator==(const WindowMinimized&) const = default;
};

/// The window left the minimized or maximized state.
struct WindowRestored {
    WindowId window{};
    bool operator==(const WindowRestored&) const = default;
};

struct WindowFocusGained {
    WindowId window{};
    bool operator==(const WindowFocusGained&) const = default;
};

struct WindowFocusLost {
    WindowId window{};
    bool operator==(const WindowFocusLost&) const = default;
};

/// A key went down. Held keys repeat after a delay, with repeat set; most game code ignores repeats.
struct KeyPressed {
    WindowId window{};
    Key key = Key::Unknown;
    bool repeat = false;
    bool operator==(const KeyPressed&) const = default;
};

struct KeyReleased {
    WindowId window{};
    Key key = Key::Unknown;
    bool operator==(const KeyReleased&) const = default;
};

// Mouse positions are in window coordinates, from the top-left corner, with +Y down. They are not
// pixels on high-density displays (see WindowResized).

struct MouseMoved {
    WindowId window{};
    glm::vec2 position{0.0f};
    /// Motion since the previous MouseMoved event.
    glm::vec2 delta{0.0f};
    bool operator==(const MouseMoved&) const = default;
};

struct MouseButtonPressed {
    WindowId window{};
    MouseButton button = MouseButton::Left;
    glm::vec2 position{0.0f};
    bool operator==(const MouseButtonPressed&) const = default;
};

struct MouseButtonReleased {
    WindowId window{};
    MouseButton button = MouseButton::Left;
    glm::vec2 position{0.0f};
    bool operator==(const MouseButtonReleased&) const = default;
};

/// Scroll amount in notches; fractional on precise touchpads. +Y scrolls up (away from the user),
/// +X scrolls right, whatever the system's "natural scrolling" setting.
struct MouseWheelScrolled {
    WindowId window{};
    glm::vec2 delta{0.0f};
    bool operator==(const MouseWheelScrolled&) const = default;
};

/// Gamepads are opened by the platform when they are plugged in, including those already plugged in
/// at startup, which are reported by the first PollEvents.
struct GamepadConnected {
    GamepadId gamepad{};
    bool operator==(const GamepadConnected&) const = default;
};

struct GamepadDisconnected {
    GamepadId gamepad{};
    bool operator==(const GamepadDisconnected&) const = default;
};

struct GamepadButtonPressed {
    GamepadId gamepad{};
    GamepadButton button = GamepadButton::South;
    bool operator==(const GamepadButtonPressed&) const = default;
};

struct GamepadButtonReleased {
    GamepadId gamepad{};
    GamepadButton button = GamepadButton::South;
    bool operator==(const GamepadButtonReleased&) const = default;
};

/// New position of an axis, normalized as described in GamepadAxis. No dead zone is applied.
struct GamepadAxisMoved {
    GamepadId gamepad{};
    GamepadAxis axis = GamepadAxis::LeftX;
    float value = 0.0f;
    bool operator==(const GamepadAxisMoved&) const = default;
};

/// Platform event, independent of SDL.
using Event = std::variant<QuitRequested, WindowCloseRequested, WindowResized, WindowMinimized, WindowRestored,
                           WindowFocusGained, WindowFocusLost, KeyPressed, KeyReleased, MouseMoved, MouseButtonPressed,
                           MouseButtonReleased, MouseWheelScrolled, GamepadConnected, GamepadDisconnected,
                           GamepadButtonPressed, GamepadButtonReleased, GamepadAxisMoved>;

} // namespace daidalos::platform
