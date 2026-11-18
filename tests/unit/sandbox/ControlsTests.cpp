// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "Controls.hpp"

using namespace daidalos;
using input::ActionMap;
using input::Device;
using platform::GamepadButton;
using platform::Key;

namespace {

constexpr auto kWindow = static_cast<platform::WindowId>(1);

const sandbox::Control& ControlNamed(std::string_view id) {
    const auto controls = sandbox::Controls();
    return *std::ranges::find_if(controls, [id](const sandbox::Control& control) { return control.id == id; });
}

class ControlsTest : public testing::Test {
protected:
    ControlsTest() { sandbox::RegisterControls(cvars); }

    void Apply(const std::vector<std::pair<std::string, std::string>>& changes) {
        for (const auto& [name, value] : changes) {
            ASSERT_TRUE(cvars.Set(name, value).has_value()) << name;
        }
    }

    core::CVarRegistry cvars;
};

} // namespace

TEST_F(ControlsTest, TheDefaultsBindEveryControlToItsOwnKey) {
    std::set<std::string> keys;
    for (const sandbox::Control& control : sandbox::Controls()) {
        const std::optional<ActionMap::Button> key = sandbox::BoundButton(cvars, control, Device::KeyboardMouse);
        ASSERT_TRUE(key.has_value()) << control.id;
        EXPECT_FALSE(sandbox::IsReserved(*key)) << control.id;
        EXPECT_TRUE(keys.insert(input::ButtonName(*key)).second) << control.id << " shares its key";
    }
    input::InputState state;
    const ActionMap actions = sandbox::MakeActions(cvars);
    state.Process(platform::KeyPressed{kWindow, Key::W, false});
    state.Process(platform::KeyPressed{kWindow, Key::A, false});
    EXPECT_FLOAT_EQ(actions.Value(state, sandbox::MoveForward), 1.0f);
    EXPECT_FLOAT_EQ(actions.Value(state, sandbox::MoveRight), -1.0f);
    state.Process(platform::KeyPressed{kWindow, Key::Escape, false});
    EXPECT_TRUE(actions.WasPressed(state, sandbox::Menu));
}

TEST_F(ControlsTest, ABoundKeyTakenFromAnotherControlGivesItTheOldOne) {
    // Jump on E, which Use had: Use takes Space.
    Apply(sandbox::Rebind(cvars, ControlNamed("jump"), Key::E));
    EXPECT_EQ(sandbox::BoundButton(cvars, ControlNamed("jump"), Device::KeyboardMouse), ActionMap::Button(Key::E));
    EXPECT_EQ(sandbox::BoundButton(cvars, ControlNamed("interact"), Device::KeyboardMouse),
              ActionMap::Button(Key::Space));
    // The gamepad's column is its own.
    EXPECT_EQ(sandbox::BoundButton(cvars, ControlNamed("jump"), Device::Gamepad),
              ActionMap::Button(GamepadButton::South));

    input::InputState state;
    const ActionMap actions = sandbox::MakeActions(cvars);
    state.Process(platform::KeyPressed{kWindow, Key::E, false});
    EXPECT_TRUE(actions.WasPressed(state, sandbox::Jump));
    EXPECT_FALSE(actions.WasPressed(state, sandbox::Interact));
}

TEST_F(ControlsTest, ReservedButtonsAndTheSameButtonChangeNothing) {
    EXPECT_TRUE(sandbox::Rebind(cvars, ControlNamed("jump"), Key::Escape).empty());
    EXPECT_TRUE(sandbox::Rebind(cvars, ControlNamed("jump"), GamepadButton::Start).empty());
    EXPECT_TRUE(sandbox::Rebind(cvars, ControlNamed("jump"), Key::Space).empty());
}

TEST_F(ControlsTest, AMovementSideRebindsAloneAndAnUnboundSideIsNeverPressed) {
    Apply(sandbox::Rebind(cvars, ControlNamed("move_forward"), Key::Up));
    ASSERT_TRUE(cvars.Set("bind.key.move_back", "").has_value());
    input::InputState state;
    const ActionMap actions = sandbox::MakeActions(cvars);
    state.Process(platform::KeyPressed{kWindow, Key::Up, false});
    state.Process(platform::KeyPressed{kWindow, Key::S, false});
    EXPECT_FLOAT_EQ(actions.Value(state, sandbox::MoveForward), 1.0f);
}

TEST_F(ControlsTest, ASettingThatNamesNoButtonOfItsDeviceKeepsTheDefault) {
    ASSERT_TRUE(cvars.Set("bind.key.reload", "Banana").has_value());
    EXPECT_EQ(sandbox::BoundButton(cvars, ControlNamed("reload"), Device::KeyboardMouse), ActionMap::Button(Key::R));
    ASSERT_TRUE(cvars.Set("bind.key.reload", "PadNorth").has_value());
    EXPECT_EQ(sandbox::BoundButton(cvars, ControlNamed("reload"), Device::KeyboardMouse), ActionMap::Button(Key::R));
    ASSERT_TRUE(cvars.Set("bind.pad.reload", "").has_value());
    EXPECT_FALSE(sandbox::BoundButton(cvars, ControlNamed("reload"), Device::Gamepad).has_value());
}

TEST_F(ControlsTest, MenusShowShortNames) {
    EXPECT_EQ(sandbox::ShortName(platform::MouseButton::Left), "LMB");
    EXPECT_EQ(sandbox::ShortName(GamepadButton::South), "A");
    EXPECT_EQ(sandbox::ShortName(GamepadButton::DpadLeft), "D-Left");
    EXPECT_EQ(sandbox::ShortName(Key::LeftShift), "LShift");
    EXPECT_EQ(sandbox::ShortName(Key::Digit1), "1");
}
