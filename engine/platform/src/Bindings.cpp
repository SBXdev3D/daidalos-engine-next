// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/input/Bindings.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>

namespace daidalos::input {

namespace {

using platform::GamepadButton;
using platform::Key;
using platform::MouseButton;

// clang-format off
constexpr std::pair<Key, std::string_view> kKeyNames[] = {
    {Key::Enter, "Enter"}, {Key::Escape, "Escape"}, {Key::Backspace, "Backspace"}, {Key::Tab, "Tab"},
    {Key::Space, "Space"}, {Key::Minus, "Minus"}, {Key::Equals, "Equals"}, {Key::LeftBracket, "LeftBracket"},
    {Key::RightBracket, "RightBracket"}, {Key::Backslash, "Backslash"}, {Key::NonUsHash, "NonUsHash"},
    {Key::Semicolon, "Semicolon"}, {Key::Apostrophe, "Apostrophe"}, {Key::Grave, "Grave"}, {Key::Comma, "Comma"},
    {Key::Period, "Period"}, {Key::Slash, "Slash"}, {Key::CapsLock, "CapsLock"},
    {Key::PrintScreen, "PrintScreen"}, {Key::ScrollLock, "ScrollLock"}, {Key::Pause, "Pause"},
    {Key::Insert, "Insert"}, {Key::Home, "Home"}, {Key::PageUp, "PageUp"}, {Key::Delete, "Delete"},
    {Key::End, "End"}, {Key::PageDown, "PageDown"}, {Key::Right, "Right"}, {Key::Left, "Left"},
    {Key::Down, "Down"}, {Key::Up, "Up"}, {Key::NumLock, "NumLock"}, {Key::NumpadDivide, "NumpadDivide"},
    {Key::NumpadMultiply, "NumpadMultiply"}, {Key::NumpadMinus, "NumpadMinus"}, {Key::NumpadPlus, "NumpadPlus"},
    {Key::NumpadEnter, "NumpadEnter"}, {Key::Numpad1, "Numpad1"}, {Key::Numpad2, "Numpad2"},
    {Key::Numpad3, "Numpad3"}, {Key::Numpad4, "Numpad4"}, {Key::Numpad5, "Numpad5"}, {Key::Numpad6, "Numpad6"},
    {Key::Numpad7, "Numpad7"}, {Key::Numpad8, "Numpad8"}, {Key::Numpad9, "Numpad9"}, {Key::Numpad0, "Numpad0"},
    {Key::NumpadPeriod, "NumpadPeriod"}, {Key::NonUsBackslash, "NonUsBackslash"}, {Key::Menu, "Menu"},
    {Key::LeftCtrl, "LeftCtrl"}, {Key::LeftShift, "LeftShift"}, {Key::LeftAlt, "LeftAlt"},
    {Key::LeftSuper, "LeftSuper"}, {Key::RightCtrl, "RightCtrl"}, {Key::RightShift, "RightShift"},
    {Key::RightAlt, "RightAlt"}, {Key::RightSuper, "RightSuper"},
    {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"}, {Key::F5, "F5"}, {Key::F6, "F6"},
    {Key::F7, "F7"}, {Key::F8, "F8"}, {Key::F9, "F9"}, {Key::F10, "F10"}, {Key::F11, "F11"}, {Key::F12, "F12"},
};
// clang-format on

constexpr std::array<std::string_view, platform::kMouseButtonCount> kMouseNames{"Left", "Middle", "Right", "X1", "X2"};
constexpr std::array<std::string_view, platform::kGamepadButtonCount> kPadNames{
    "South",      "East",         "West",          "North",  "Back",     "Guide",    "Start",    "LeftStick",
    "RightStick", "LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight"};

bool SameText(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && SameText(text.substr(0, prefix.size()), prefix);
}

std::string KeyName(Key key) {
    const auto value = static_cast<unsigned>(key);
    if (key >= Key::A && key <= Key::Z) {
        return std::string(1, static_cast<char>('A' + (value - static_cast<unsigned>(Key::A))));
    }
    if (key >= Key::Digit1 && key <= Key::Digit0) {
        // Digit1 to Digit9, then Digit0.
        const unsigned digit = (value - static_cast<unsigned>(Key::Digit1) + 1) % 10;
        return std::format("Digit{}", digit);
    }
    for (const auto& [named, name] : kKeyNames) {
        if (named == key) {
            return std::string(name);
        }
    }
    return std::format("Key{}", value);
}

std::optional<Key> ParseKey(std::string_view name) {
    if (name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0])) != 0) {
        const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        return static_cast<Key>(static_cast<unsigned>(Key::A) + static_cast<unsigned>(letter - 'A'));
    }
    if (name.size() == 6 && StartsWith(name, "Digit") && std::isdigit(static_cast<unsigned char>(name[5])) != 0) {
        const unsigned digit = static_cast<unsigned>(name[5] - '0');
        return digit == 0 ? Key::Digit0 : static_cast<Key>(static_cast<unsigned>(Key::Digit1) + digit - 1);
    }
    for (const auto& [key, keyName] : kKeyNames) {
        if (SameText(keyName, name)) {
            return key;
        }
    }
    if (StartsWith(name, "Key") && name.size() > 3) {
        unsigned value = 0;
        const std::string_view digits = name.substr(3);
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (error == std::errc{} && end == digits.data() + digits.size() && value > 0 && value < platform::kKeyCount) {
            return static_cast<Key>(value);
        }
    }
    return std::nullopt;
}

} // namespace

Device DeviceOf(const ActionMap::Button& button) {
    return std::holds_alternative<GamepadButton>(button) ? Device::Gamepad : Device::KeyboardMouse;
}

std::string ButtonName(const ActionMap::Button& button) {
    if (const auto* key = std::get_if<Key>(&button)) {
        return KeyName(*key);
    }
    if (const auto* mouse = std::get_if<MouseButton>(&button)) {
        return std::format("Mouse{}", kMouseNames.at(static_cast<std::size_t>(*mouse)));
    }
    return std::format("Pad{}", kPadNames.at(static_cast<std::size_t>(std::get<GamepadButton>(button))));
}

std::optional<ActionMap::Button> ParseButton(std::string_view name) {
    if (StartsWith(name, "Mouse")) {
        const std::string_view rest = name.substr(5);
        for (std::size_t index = 0; index < kMouseNames.size(); ++index) {
            if (SameText(kMouseNames[index], rest)) {
                return static_cast<MouseButton>(index);
            }
        }
        return std::nullopt;
    }
    if (StartsWith(name, "Pad")) {
        const std::string_view rest = name.substr(3);
        for (std::size_t index = 0; index < kPadNames.size(); ++index) {
            if (SameText(kPadNames[index], rest)) {
                return static_cast<GamepadButton>(index);
            }
        }
        return std::nullopt;
    }
    if (const std::optional<Key> key = ParseKey(name)) {
        return *key;
    }
    return std::nullopt;
}

std::optional<ActionMap::Button> FirstPressed(const InputState& state, Device device) {
    if (device == Device::KeyboardMouse) {
        for (std::size_t value = 1; value < platform::kKeyCount; ++value) {
            if (state.WasPressed(static_cast<Key>(value))) {
                return static_cast<Key>(value);
            }
        }
        for (std::size_t value = 0; value < platform::kMouseButtonCount; ++value) {
            if (state.WasPressed(static_cast<MouseButton>(value))) {
                return static_cast<MouseButton>(value);
            }
        }
        return std::nullopt;
    }
    for (const platform::GamepadId gamepad : state.Gamepads()) {
        for (std::size_t value = 0; value < platform::kGamepadButtonCount; ++value) {
            if (state.WasPressed(gamepad, static_cast<GamepadButton>(value))) {
                return static_cast<GamepadButton>(value);
            }
        }
    }
    return std::nullopt;
}

} // namespace daidalos::input
