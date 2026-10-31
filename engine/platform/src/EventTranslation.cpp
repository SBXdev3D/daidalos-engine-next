// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "EventTranslation.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace daidalos::platform::detail {

namespace {

// Key values are USB HID usage IDs, and so are SDL scancodes: a scancode converts without a table.
static_assert(static_cast<std::size_t>(SDL_SCANCODE_COUNT) == kKeyCount);
static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A);
static_assert(static_cast<int>(Key::Z) == SDL_SCANCODE_Z);
static_assert(static_cast<int>(Key::Digit1) == SDL_SCANCODE_1);
static_assert(static_cast<int>(Key::Digit0) == SDL_SCANCODE_0);
static_assert(static_cast<int>(Key::Enter) == SDL_SCANCODE_RETURN);
static_assert(static_cast<int>(Key::Escape) == SDL_SCANCODE_ESCAPE);
static_assert(static_cast<int>(Key::Space) == SDL_SCANCODE_SPACE);
static_assert(static_cast<int>(Key::NonUsHash) == SDL_SCANCODE_NONUSHASH);
static_assert(static_cast<int>(Key::CapsLock) == SDL_SCANCODE_CAPSLOCK);
static_assert(static_cast<int>(Key::F1) == SDL_SCANCODE_F1);
static_assert(static_cast<int>(Key::F12) == SDL_SCANCODE_F12);
static_assert(static_cast<int>(Key::PrintScreen) == SDL_SCANCODE_PRINTSCREEN);
static_assert(static_cast<int>(Key::Up) == SDL_SCANCODE_UP);
static_assert(static_cast<int>(Key::NumLock) == SDL_SCANCODE_NUMLOCKCLEAR);
static_assert(static_cast<int>(Key::Numpad1) == SDL_SCANCODE_KP_1);
static_assert(static_cast<int>(Key::Numpad0) == SDL_SCANCODE_KP_0);
static_assert(static_cast<int>(Key::NumpadPeriod) == SDL_SCANCODE_KP_PERIOD);
static_assert(static_cast<int>(Key::NonUsBackslash) == SDL_SCANCODE_NONUSBACKSLASH);
static_assert(static_cast<int>(Key::Menu) == SDL_SCANCODE_APPLICATION);
static_assert(static_cast<int>(Key::LeftCtrl) == SDL_SCANCODE_LCTRL);
static_assert(static_cast<int>(Key::RightSuper) == SDL_SCANCODE_RGUI);

std::uint32_t NonNegative(Sint32 value) {
    return static_cast<std::uint32_t>(std::max<Sint32>(value, 0));
}

std::optional<Key> TranslateKey(SDL_Scancode scancode) {
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT) {
        return std::nullopt;
    }
    return static_cast<Key>(scancode);
}

std::optional<MouseButton> TranslateMouseButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

std::optional<GamepadButton> TranslateGamepadButton(Uint8 button) {
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    default:
        return std::nullopt; // Paddles, touchpad and miscellaneous buttons are not mapped.
    }
}

std::optional<GamepadAxisMoved> TranslateGamepadAxis(const SDL_GamepadAxisEvent& event) {
    const auto gamepad = static_cast<GamepadId>(event.which);
    // SDL sticks range over [-32768, 32767] with +Y down; triggers over [0, 32767].
    const float raw = std::clamp(static_cast<float>(event.value) / 32767.0f, -1.0f, 1.0f);
    switch (static_cast<SDL_GamepadAxis>(event.axis)) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxisMoved{gamepad, GamepadAxis::LeftX, raw};
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxisMoved{gamepad, GamepadAxis::LeftY, -raw};
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxisMoved{gamepad, GamepadAxis::RightX, raw};
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxisMoved{gamepad, GamepadAxis::RightY, -raw};
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxisMoved{gamepad, GamepadAxis::LeftTrigger, std::max(raw, 0.0f)};
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxisMoved{gamepad, GamepadAxis::RightTrigger, std::max(raw, 0.0f)};
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<Event> TranslateEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return QuitRequested{};

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return WindowCloseRequested{static_cast<WindowId>(event.window.windowID)};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return WindowResized{static_cast<WindowId>(event.window.windowID),
                             Extent{NonNegative(event.window.data1), NonNegative(event.window.data2)}};
    case SDL_EVENT_WINDOW_MINIMIZED:
        return WindowMinimized{static_cast<WindowId>(event.window.windowID)};
    case SDL_EVENT_WINDOW_RESTORED:
        return WindowRestored{static_cast<WindowId>(event.window.windowID)};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return WindowFocusGained{static_cast<WindowId>(event.window.windowID)};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return WindowFocusLost{static_cast<WindowId>(event.window.windowID)};

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const std::optional<Key> key = TranslateKey(event.key.scancode);
        if (!key) {
            return std::nullopt;
        }
        const auto window = static_cast<WindowId>(event.key.windowID);
        if (event.type == SDL_EVENT_KEY_DOWN) {
            return KeyPressed{window, *key, event.key.repeat};
        }
        return KeyReleased{window, *key};
    }

    case SDL_EVENT_MOUSE_MOTION:
        return MouseMoved{static_cast<WindowId>(event.motion.windowID), glm::vec2(event.motion.x, event.motion.y),
                          glm::vec2(event.motion.xrel, event.motion.yrel)};
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const std::optional<MouseButton> button = TranslateMouseButton(event.button.button);
        if (!button) {
            return std::nullopt;
        }
        const auto window = static_cast<WindowId>(event.button.windowID);
        const glm::vec2 position(event.button.x, event.button.y);
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            return MouseButtonPressed{window, *button, position};
        }
        return MouseButtonReleased{window, *button, position};
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const float sign = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
        return MouseWheelScrolled{static_cast<WindowId>(event.wheel.windowID),
                                  glm::vec2(event.wheel.x, event.wheel.y) * sign};
    }

    case SDL_EVENT_GAMEPAD_ADDED:
        return GamepadConnected{static_cast<GamepadId>(event.gdevice.which)};
    case SDL_EVENT_GAMEPAD_REMOVED:
        return GamepadDisconnected{static_cast<GamepadId>(event.gdevice.which)};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const std::optional<GamepadButton> button = TranslateGamepadButton(event.gbutton.button);
        if (!button) {
            return std::nullopt;
        }
        const auto gamepad = static_cast<GamepadId>(event.gbutton.which);
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            return GamepadButtonPressed{gamepad, *button};
        }
        return GamepadButtonReleased{gamepad, *button};
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return TranslateGamepadAxis(event.gaxis);

    default:
        return std::nullopt;
    }
}

} // namespace daidalos::platform::detail
