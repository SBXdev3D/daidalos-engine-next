// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <gtest/gtest.h>

#include <optional>
#include <variant>

#include "EventTranslation.hpp"

using namespace daidalos::platform;
using detail::TranslateEvent;

namespace {

constexpr auto kWindow = static_cast<WindowId>(7);

constexpr auto kGamepad = static_cast<GamepadId>(3);

SDL_Event KeyEvent(SDL_EventType type, SDL_Scancode scancode, bool repeat = false) {
    SDL_Event event{};
    event.type = type;
    event.key.windowID = 7;
    event.key.scancode = scancode;
    event.key.down = (type == SDL_EVENT_KEY_DOWN);
    event.key.repeat = repeat;
    return event;
}

SDL_Event MouseButtonEvent(SDL_EventType type, Uint8 button) {
    SDL_Event event{};
    event.type = type;
    event.button.windowID = 7;
    event.button.button = button;
    event.button.x = 10.0f;
    event.button.y = 20.0f;
    return event;
}

SDL_Event GamepadButtonEvent(SDL_EventType type, SDL_GamepadButton button) {
    SDL_Event event{};
    event.type = type;
    event.gbutton.which = 3;
    event.gbutton.button = static_cast<Uint8>(button);
    return event;
}

SDL_Event GamepadAxisEvent(SDL_GamepadAxis axis, Sint16 value) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = 3;
    event.gaxis.axis = static_cast<Uint8>(axis);
    event.gaxis.value = value;
    return event;
}

std::optional<float> AxisValue(SDL_GamepadAxis axis, Sint16 value) {
    const std::optional<Event> event = TranslateEvent(GamepadAxisEvent(axis, value));
    if (!event || !std::holds_alternative<GamepadAxisMoved>(*event)) {
        return std::nullopt;
    }
    return std::get<GamepadAxisMoved>(*event).value;
}

SDL_Event WindowEvent(SDL_EventType type, Sint32 data1 = 0, Sint32 data2 = 0) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = 7;
    event.window.data1 = data1;
    event.window.data2 = data2;
    return event;
}

} // namespace

TEST(EventTranslation, QuitBecomesQuitRequested) {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;

    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(QuitRequested{}));
}

TEST(EventTranslation, DroppedFileBecomesFileDropped) {
    char path[] = "C:/models/crate.gltf";
    SDL_Event event{};
    event.type = SDL_EVENT_DROP_FILE;
    event.drop.windowID = static_cast<SDL_WindowID>(kWindow);
    event.drop.x = 10.0f;
    event.drop.y = 20.0f;
    event.drop.data = path;
    EXPECT_EQ(TranslateEvent(event),
              std::optional<Event>(FileDropped{kWindow, "C:/models/crate.gltf", glm::vec2(10.0f, 20.0f)}));
}

TEST(EventTranslation, TextInputBecomesTextEntered) {
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = 7;
    const char* text = "\xC3\xA9t\xC3\xA9"; // "été" in UTF-8.
    event.text.text = text;
    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(TextEntered{kWindow, text}));

    event.text.text = "";
    EXPECT_EQ(TranslateEvent(event), std::nullopt) << "Empty text is no event";
}

TEST(EventTranslation, WindowEventsKeepTheirWindowId) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED)),
              std::optional<Event>(WindowCloseRequested{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_MINIMIZED)), std::optional<Event>(WindowMinimized{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_RESTORED)), std::optional<Event>(WindowRestored{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED)),
              std::optional<Event>(WindowFocusGained{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST)), std::optional<Event>(WindowFocusLost{kWindow}));
}

TEST(EventTranslation, PixelSizeChangeBecomesResize) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, 1920, 1080)),
              std::optional<Event>(WindowResized{kWindow, Extent{1920, 1080}}));
}

TEST(EventTranslation, NegativeSizeIsClampedToZero) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, -5, 10)),
              std::optional<Event>(WindowResized{kWindow, Extent{0, 10}}));
}

TEST(EventTranslation, LogicalResizeIsIgnoredInFavourOfPixelResize) {
    // SDL sends both; only the pixel size matters for rendering.
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_RESIZED, 800, 600)), std::nullopt);
}

TEST(EventTranslation, UnusedEventsAreDropped) {
    SDL_Event event{};
    event.type = SDL_EVENT_CLIPBOARD_UPDATE;

    EXPECT_EQ(TranslateEvent(event), std::nullopt);
}

TEST(EventTranslation, KeyDownAndUpUseThePhysicalKey) {
    EXPECT_EQ(TranslateEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W)),
              std::optional<Event>(KeyPressed{kWindow, Key::W, false}));
    EXPECT_EQ(TranslateEvent(KeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_ESCAPE)),
              std::optional<Event>(KeyReleased{kWindow, Key::Escape}));
}

TEST(EventTranslation, KeyRepeatIsFlagged) {
    EXPECT_EQ(TranslateEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE, true)),
              std::optional<Event>(KeyPressed{kWindow, Key::Space, true}));
}

TEST(EventTranslation, UnnamedKeysKeepTheirUsageId) {
    const std::optional<Event> event = TranslateEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_MUTE));
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(static_cast<int>(std::get<KeyPressed>(*event).key), SDL_SCANCODE_MUTE);
}

TEST(EventTranslation, UnknownScancodeIsDropped) {
    EXPECT_EQ(TranslateEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_UNKNOWN)), std::nullopt);
}

TEST(EventTranslation, MouseMotionKeepsPositionAndDelta) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 7;
    event.motion.x = 100.5f;
    event.motion.y = 50.0f;
    event.motion.xrel = -2.0f;
    event.motion.yrel = 3.0f;

    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(MouseMoved{kWindow, {100.5f, 50.0f}, {-2.0f, 3.0f}}));
}

TEST(EventTranslation, MouseButtonsAreMapped) {
    EXPECT_EQ(TranslateEvent(MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT)),
              std::optional<Event>(MouseButtonPressed{kWindow, MouseButton::Left, {10.0f, 20.0f}}));
    EXPECT_EQ(TranslateEvent(MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT)),
              std::optional<Event>(MouseButtonReleased{kWindow, MouseButton::Right, {10.0f, 20.0f}}));
    EXPECT_EQ(TranslateEvent(MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_X2)),
              std::optional<Event>(MouseButtonPressed{kWindow, MouseButton::X2, {10.0f, 20.0f}}));
}

TEST(EventTranslation, UnknownMouseButtonIsDropped) {
    EXPECT_EQ(TranslateEvent(MouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, 9)), std::nullopt);
}

TEST(EventTranslation, WheelIsNormalizedForFlippedDirection) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = 7;
    event.wheel.y = 1.0f;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(MouseWheelScrolled{kWindow, {0.0f, 1.0f}}));

    // With "natural scrolling", SDL reports the opposite sign and flags it.
    event.wheel.y = -1.0f;
    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(MouseWheelScrolled{kWindow, {0.0f, 1.0f}}));
}

TEST(EventTranslation, GamepadConnectionEvents) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 3;
    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(GamepadConnected{kGamepad}));
    event.type = SDL_EVENT_GAMEPAD_REMOVED;
    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(GamepadDisconnected{kGamepad}));
}

TEST(EventTranslation, GamepadButtonsAreMappedByPosition) {
    EXPECT_EQ(TranslateEvent(GamepadButtonEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH)),
              std::optional<Event>(GamepadButtonPressed{kGamepad, GamepadButton::South}));
    EXPECT_EQ(TranslateEvent(GamepadButtonEvent(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)),
              std::optional<Event>(GamepadButtonReleased{kGamepad, GamepadButton::DpadRight}));
}

TEST(EventTranslation, UnmappedGamepadButtonIsDropped) {
    EXPECT_EQ(TranslateEvent(GamepadButtonEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_TOUCHPAD)),
              std::nullopt);
}

TEST(EventTranslation, StickAxesAreNormalizedWithYUp) {
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_LEFTX, 32767), 1.0f);
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_LEFTX, -32768), -1.0f);
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_LEFTX, 0), 0.0f);
    // SDL reports pushing the stick up as negative.
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_LEFTY, -32768), 1.0f);
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_RIGHTY, 32767), -1.0f);
}

TEST(EventTranslation, TriggersAreNormalizedToZeroOne) {
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0), 0.0f);
    EXPECT_EQ(AxisValue(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767), 1.0f);
}
