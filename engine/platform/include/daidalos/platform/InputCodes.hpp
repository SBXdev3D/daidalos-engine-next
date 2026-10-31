// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace daidalos::platform {

/// A physical key, named after its position on a US QWERTY keyboard: Key::W is the key above S
/// whatever the layout prints on it (Z on AZERTY). This is what movement controls need. Text entry
/// will use separate text events.
///
/// Values are USB HID keyboard usage IDs, a hardware standard. Keys without a name here (media keys,
/// for example) still arrive with their numeric value, below kKeyCount.
// Formatting is off so that the letter, digit and function key runs stay on one line each.
// clang-format off
enum class Key : std::uint16_t {
    Unknown = 0,

    A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit1 = 30, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Digit0,

    Enter = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,
    Minus = 45,
    Equals = 46,
    LeftBracket = 47,
    RightBracket = 48,
    Backslash = 49,
    NonUsHash = 50,
    Semicolon = 51,
    Apostrophe = 52,
    Grave = 53,
    Comma = 54,
    Period = 55,
    Slash = 56,
    CapsLock = 57,

    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    PrintScreen = 70,
    ScrollLock = 71,
    Pause = 72,
    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    NumLock = 83,
    NumpadDivide = 84,
    NumpadMultiply = 85,
    NumpadMinus = 86,
    NumpadPlus = 87,
    NumpadEnter = 88,
    Numpad1 = 89, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9, Numpad0,
    NumpadPeriod = 99,

    /// The extra key next to left Shift on ISO keyboards (< > on AZERTY).
    NonUsBackslash = 100,
    Menu = 101,

    LeftCtrl = 224,
    LeftShift = 225,
    LeftAlt = 226,
    LeftSuper = 227,
    RightCtrl = 228,
    RightShift = 229,
    RightAlt = 230,
    RightSuper = 231,
};
// clang-format on

/// Every Key value is below this.
inline constexpr std::size_t kKeyCount = 512;

enum class MouseButton : std::uint8_t {
    Left,
    Middle,
    Right,
    /// Side buttons, usually "back" and "forward".
    X1,
    X2,
};

inline constexpr std::size_t kMouseButtonCount = 5;

/// Identifies a connected gamepad in events. Never 0 for a connected gamepad. A gamepad that is
/// unplugged and plugged back gets a new id.
enum class GamepadId : std::uint32_t {};

/// Buttons named by position, so that layouts with different labels (A/B, Cross/Circle) agree.
enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
};

inline constexpr std::size_t kGamepadButtonCount = 15;

enum class GamepadAxis : std::uint8_t {
    /// Sticks are in [-1, 1]: +1 is right for X axes and up for Y axes.
    LeftX,
    LeftY,
    RightX,
    RightY,
    /// Triggers are in [0, 1]: 0 is released.
    LeftTrigger,
    RightTrigger,
};

inline constexpr std::size_t kGamepadAxisCount = 6;

} // namespace daidalos::platform
