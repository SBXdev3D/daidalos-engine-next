// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/input/InputState.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace daidalos;
using input::InputState;
using platform::GamepadAxis;
using platform::GamepadButton;
using platform::GamepadId;
using platform::Key;
using platform::MouseButton;

namespace {

constexpr auto kWindow = static_cast<platform::WindowId>(1);
constexpr auto kPad = static_cast<GamepadId>(4);
constexpr auto kOtherPad = static_cast<GamepadId>(5);

platform::KeyPressed Press(Key key, bool repeat = false) {
    return platform::KeyPressed{kWindow, key, repeat};
}

platform::KeyReleased Release(Key key) {
    return platform::KeyReleased{kWindow, key};
}

} // namespace

TEST(KeyboardState, NothingIsDownInitially) {
    const InputState input;
    EXPECT_FALSE(input.IsDown(Key::W));
    EXPECT_FALSE(input.WasPressed(Key::W));
    EXPECT_FALSE(input.WasReleased(Key::W));
}

TEST(KeyboardState, PressIsDownAndPressedThisFrame) {
    InputState input;
    input.BeginFrame();
    input.Process(Press(Key::W));

    EXPECT_TRUE(input.IsDown(Key::W));
    EXPECT_TRUE(input.WasPressed(Key::W));
    EXPECT_FALSE(input.WasReleased(Key::W));
    EXPECT_FALSE(input.IsDown(Key::S));
}

TEST(KeyboardState, PressedOnlyLastsOneFrame) {
    InputState input;
    input.BeginFrame();
    input.Process(Press(Key::W));
    input.BeginFrame();

    EXPECT_TRUE(input.IsDown(Key::W));
    EXPECT_FALSE(input.WasPressed(Key::W));
}

TEST(KeyboardState, ReleaseIsUpAndReleasedThisFrame) {
    InputState input;
    input.BeginFrame();
    input.Process(Press(Key::W));
    input.BeginFrame();
    input.Process(Release(Key::W));

    EXPECT_FALSE(input.IsDown(Key::W));
    EXPECT_TRUE(input.WasReleased(Key::W));
    input.BeginFrame();
    EXPECT_FALSE(input.WasReleased(Key::W));
}

TEST(KeyboardState, TapWithinOneFrameIsNotLost) {
    InputState input;
    input.BeginFrame();
    input.Process(Press(Key::Space));
    input.Process(Release(Key::Space));

    EXPECT_FALSE(input.IsDown(Key::Space));
    EXPECT_TRUE(input.WasPressed(Key::Space));
    EXPECT_TRUE(input.WasReleased(Key::Space));
}

TEST(KeyboardState, RepeatsAreNotNewPresses) {
    InputState input;
    input.BeginFrame();
    input.Process(Press(Key::A));
    input.BeginFrame();
    input.Process(Press(Key::A, /*repeat=*/true));

    EXPECT_TRUE(input.IsDown(Key::A));
    EXPECT_FALSE(input.WasPressed(Key::A));
}

TEST(KeyboardState, ReleaseWithoutPressIsIgnored) {
    InputState input;
    input.BeginFrame();
    input.Process(Release(Key::A));
    EXPECT_FALSE(input.WasReleased(Key::A));
}

TEST(KeyboardState, UnnamedKeysAreTracked) {
    InputState input;
    const auto mute = static_cast<Key>(127);
    input.Process(Press(mute));
    EXPECT_TRUE(input.IsDown(mute));
}

TEST(KeyboardState, OutOfRangeKeyIsIgnored) {
    InputState input;
    const auto invalid = static_cast<Key>(platform::kKeyCount + 10);
    input.Process(Press(invalid));
    EXPECT_FALSE(input.IsDown(invalid));
}

TEST(MouseState, ButtonsFollowPressAndRelease) {
    InputState input;
    input.BeginFrame();
    input.Process(platform::MouseButtonPressed{kWindow, MouseButton::Right, {5.0f, 6.0f}});

    EXPECT_TRUE(input.IsDown(MouseButton::Right));
    EXPECT_TRUE(input.WasPressed(MouseButton::Right));
    EXPECT_FALSE(input.IsDown(MouseButton::Left));

    input.BeginFrame();
    input.Process(platform::MouseButtonReleased{kWindow, MouseButton::Right, {5.0f, 6.0f}});
    EXPECT_FALSE(input.IsDown(MouseButton::Right));
    EXPECT_TRUE(input.WasReleased(MouseButton::Right));
}

TEST(MouseState, MotionUpdatesPositionAndAccumulatesDelta) {
    InputState input;
    input.BeginFrame();
    input.Process(platform::MouseMoved{kWindow, {10.0f, 10.0f}, {1.0f, 2.0f}});
    input.Process(platform::MouseMoved{kWindow, {13.0f, 9.0f}, {3.0f, -1.0f}});

    EXPECT_EQ(input.MousePosition(), glm::vec2(13.0f, 9.0f));
    EXPECT_EQ(input.MouseDelta(), glm::vec2(4.0f, 1.0f));

    input.BeginFrame();
    EXPECT_EQ(input.MouseDelta(), glm::vec2(0.0f));
    EXPECT_EQ(input.MousePosition(), glm::vec2(13.0f, 9.0f));
}

TEST(MouseState, ButtonEventsUpdatePosition) {
    InputState input;
    input.Process(platform::MouseButtonPressed{kWindow, MouseButton::Left, {40.0f, 30.0f}});
    EXPECT_EQ(input.MousePosition(), glm::vec2(40.0f, 30.0f));
}

TEST(MouseState, WheelAccumulatesWithinAFrame) {
    InputState input;
    input.BeginFrame();
    input.Process(platform::MouseWheelScrolled{kWindow, {0.0f, 1.0f}});
    input.Process(platform::MouseWheelScrolled{kWindow, {0.0f, 0.5f}});
    EXPECT_EQ(input.WheelDelta(), glm::vec2(0.0f, 1.5f));

    input.BeginFrame();
    EXPECT_EQ(input.WheelDelta(), glm::vec2(0.0f));
}

TEST(GamepadState, ConnectedGamepadsAreListedInOrder) {
    InputState input;
    input.Process(platform::GamepadConnected{kPad});
    input.Process(platform::GamepadConnected{kOtherPad});
    input.Process(platform::GamepadConnected{kPad}); // Duplicate connections are ignored.

    EXPECT_EQ(input.Gamepads(), (std::vector<GamepadId>{kPad, kOtherPad}));
    EXPECT_TRUE(input.IsConnected(kPad));
}

TEST(GamepadState, ButtonsAreTrackedPerGamepad) {
    InputState input;
    input.Process(platform::GamepadConnected{kPad});
    input.Process(platform::GamepadConnected{kOtherPad});
    input.BeginFrame();
    input.Process(platform::GamepadButtonPressed{kPad, GamepadButton::South});

    EXPECT_TRUE(input.IsDown(kPad, GamepadButton::South));
    EXPECT_TRUE(input.WasPressed(kPad, GamepadButton::South));
    EXPECT_FALSE(input.IsDown(kOtherPad, GamepadButton::South));

    input.BeginFrame();
    input.Process(platform::GamepadButtonReleased{kPad, GamepadButton::South});
    EXPECT_FALSE(input.IsDown(kPad, GamepadButton::South));
    EXPECT_TRUE(input.WasReleased(kPad, GamepadButton::South));
}

TEST(GamepadState, AxesKeepTheirLastValue) {
    InputState input;
    input.Process(platform::GamepadConnected{kPad});
    input.Process(platform::GamepadAxisMoved{kPad, GamepadAxis::LeftY, 0.75f});
    input.BeginFrame();

    EXPECT_EQ(input.Axis(kPad, GamepadAxis::LeftY), 0.75f);
    EXPECT_EQ(input.Axis(kPad, GamepadAxis::LeftX), 0.0f);
}

TEST(GamepadState, EventsOfUnknownGamepadsAreIgnored) {
    InputState input;
    input.Process(platform::GamepadButtonPressed{kPad, GamepadButton::North});
    input.Process(platform::GamepadAxisMoved{kPad, GamepadAxis::RightX, 1.0f});

    EXPECT_FALSE(input.IsConnected(kPad));
    EXPECT_FALSE(input.IsDown(kPad, GamepadButton::North));
    EXPECT_EQ(input.Axis(kPad, GamepadAxis::RightX), 0.0f);
}

TEST(GamepadState, DisconnectionForgetsTheGamepad) {
    InputState input;
    input.Process(platform::GamepadConnected{kPad});
    input.Process(platform::GamepadButtonPressed{kPad, GamepadButton::Start});

    input.Process(platform::GamepadDisconnected{kPad});

    EXPECT_FALSE(input.IsConnected(kPad));
    EXPECT_FALSE(input.IsDown(kPad, GamepadButton::Start));
    EXPECT_TRUE(input.Gamepads().empty());
}

TEST(FocusLoss, ReleasesEverythingThatWasHeld) {
    InputState input;
    input.Process(platform::GamepadConnected{kPad});
    input.Process(Press(Key::W));
    input.Process(platform::MouseButtonPressed{kWindow, MouseButton::Left, {0.0f, 0.0f}});
    input.Process(platform::GamepadButtonPressed{kPad, GamepadButton::East});
    input.Process(platform::GamepadAxisMoved{kPad, GamepadAxis::LeftX, -1.0f});
    input.BeginFrame();

    input.Process(platform::WindowFocusLost{kWindow});

    EXPECT_FALSE(input.IsDown(Key::W));
    EXPECT_TRUE(input.WasReleased(Key::W));
    EXPECT_FALSE(input.IsDown(MouseButton::Left));
    EXPECT_TRUE(input.WasReleased(MouseButton::Left));
    EXPECT_FALSE(input.IsDown(kPad, GamepadButton::East));
    EXPECT_EQ(input.Axis(kPad, GamepadAxis::LeftX), 0.0f);
    EXPECT_TRUE(input.IsConnected(kPad));
    // Keys that were not held are not reported as released.
    EXPECT_FALSE(input.WasReleased(Key::S));
}

TEST(FocusLoss, KeysCanBePressedAgainAfterwards) {
    InputState input;
    input.Process(Press(Key::W));
    input.Process(platform::WindowFocusLost{kWindow});
    input.BeginFrame();

    input.Process(Press(Key::W));

    EXPECT_TRUE(input.WasPressed(Key::W));
}

TEST(InputStateEvents, WindowEventsDoNotChangeInput) {
    InputState input;
    input.Process(Press(Key::W));
    input.Process(platform::WindowResized{kWindow, {800, 600}});
    input.Process(platform::QuitRequested{});
    EXPECT_TRUE(input.IsDown(Key::W));
}
