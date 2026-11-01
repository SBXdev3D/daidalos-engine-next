// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/input/ActionMap.hpp>

#include <gtest/gtest.h>

using namespace daidalos;
using input::ActionMap;
using input::InputState;
using platform::GamepadAxis;
using platform::GamepadButton;
using platform::GamepadId;
using platform::Key;
using platform::MouseButton;

namespace {

constexpr auto kWindow = static_cast<platform::WindowId>(1);
constexpr auto kPad = static_cast<GamepadId>(4);

enum Action : input::Action { Fire, Strafe, Unbound };

class ActionMapTest : public testing::Test {
protected:
    void PressKey(Key key) { state.Process(platform::KeyPressed{kWindow, key, false}); }
    void ConnectPad() { state.Process(platform::GamepadConnected{kPad}); }
    void MoveAxis(GamepadAxis axis, float value) { state.Process(platform::GamepadAxisMoved{kPad, axis, value}); }

    InputState state;
    ActionMap actions;
};

TEST_F(ActionMapTest, ActionFollowsItsKey) {
    actions.Bind(Fire, Key::Space);
    EXPECT_FALSE(actions.IsDown(state, Fire));
    PressKey(Key::Space);
    EXPECT_TRUE(actions.IsDown(state, Fire));
    EXPECT_TRUE(actions.WasPressed(state, Fire));
    state.BeginFrame();
    EXPECT_TRUE(actions.IsDown(state, Fire));
    EXPECT_FALSE(actions.WasPressed(state, Fire));
}

TEST_F(ActionMapTest, AnyBoundControlTriggersTheAction) {
    actions.Bind(Fire, Key::Space);
    actions.Bind(Fire, MouseButton::Left);
    actions.Bind(Fire, GamepadButton::South);
    state.Process(platform::MouseButtonPressed{kWindow, MouseButton::Left, {}});
    EXPECT_TRUE(actions.IsDown(state, Fire));

    state.BeginFrame();
    state.Process(platform::MouseButtonReleased{kWindow, MouseButton::Left, {}});
    ConnectPad();
    state.Process(platform::GamepadButtonPressed{kPad, GamepadButton::South});
    EXPECT_TRUE(actions.WasPressed(state, Fire));
}

TEST_F(ActionMapTest, UnboundActionsAreIdle) {
    EXPECT_FALSE(actions.IsDown(state, Unbound));
    EXPECT_FALSE(actions.WasPressed(state, Unbound));
    EXPECT_EQ(actions.Value(state, Unbound), 0.0f);
}

TEST_F(ActionMapTest, TwoKeysMakeAnAxis) {
    actions.BindAxis(Strafe, Key::D, Key::A);
    EXPECT_EQ(actions.Value(state, Strafe), 0.0f);
    PressKey(Key::D);
    EXPECT_EQ(actions.Value(state, Strafe), 1.0f);
    PressKey(Key::A);
    EXPECT_EQ(actions.Value(state, Strafe), 0.0f);
}

TEST_F(ActionMapTest, GamepadAxisHasADeadZoneAndAScale) {
    actions.BindAxis(Strafe, GamepadAxis::LeftX, -0.5f);
    ConnectPad();
    MoveAxis(GamepadAxis::LeftX, 0.1f);
    EXPECT_EQ(actions.Value(state, Strafe), 0.0f);
    MoveAxis(GamepadAxis::LeftX, 0.8f);
    EXPECT_FLOAT_EQ(actions.Value(state, Strafe), -0.4f);
}

TEST_F(ActionMapTest, AxisStaysBetweenMinusOneAndOne) {
    actions.BindAxis(Strafe, Key::D, Key::A);
    actions.BindAxis(Strafe, GamepadAxis::LeftX);
    ConnectPad();
    PressKey(Key::D);
    MoveAxis(GamepadAxis::LeftX, 1.0f);
    EXPECT_EQ(actions.Value(state, Strafe), 1.0f);
}

} // namespace
