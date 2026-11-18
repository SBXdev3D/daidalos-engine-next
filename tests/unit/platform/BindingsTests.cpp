// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/input/Bindings.hpp>
#include <daidalos/input/Rumble.hpp>

#include <gtest/gtest.h>

using namespace daidalos;
using input::ActionMap;
using input::Device;
using platform::GamepadButton;
using platform::Key;
using platform::MouseButton;

namespace {

constexpr auto kWindow = static_cast<platform::WindowId>(1);
constexpr auto kPad = static_cast<platform::GamepadId>(3);

} // namespace

TEST(BindingsTest, EveryControlHasANameThatReadsBack) {
    for (std::size_t value = 1; value < platform::kKeyCount; ++value) {
        const ActionMap::Button key = static_cast<Key>(value);
        const std::optional<ActionMap::Button> back = input::ParseButton(input::ButtonName(key));
        ASSERT_TRUE(back.has_value()) << input::ButtonName(key);
        EXPECT_EQ(*back, key) << input::ButtonName(key);
    }
    for (std::size_t value = 0; value < platform::kMouseButtonCount; ++value) {
        const ActionMap::Button mouse = static_cast<MouseButton>(value);
        EXPECT_EQ(input::ParseButton(input::ButtonName(mouse)), mouse);
    }
    for (std::size_t value = 0; value < platform::kGamepadButtonCount; ++value) {
        const ActionMap::Button pad = static_cast<GamepadButton>(value);
        EXPECT_EQ(input::ParseButton(input::ButtonName(pad)), pad);
    }
}

TEST(BindingsTest, NamesAreThoseOfConfigFilesWhateverTheirCase) {
    EXPECT_EQ(input::ButtonName(Key::W), "W");
    EXPECT_EQ(input::ButtonName(Key::Digit0), "Digit0");
    EXPECT_EQ(input::ButtonName(Key::LeftShift), "LeftShift");
    EXPECT_EQ(input::ButtonName(MouseButton::Right), "MouseRight");
    EXPECT_EQ(input::ButtonName(GamepadButton::RightShoulder), "PadRightShoulder");
    EXPECT_EQ(input::ParseButton("space"), ActionMap::Button(Key::Space));
    EXPECT_EQ(input::ParseButton("padsouth"), ActionMap::Button(GamepadButton::South));
    EXPECT_FALSE(input::ParseButton("").has_value());
    EXPECT_FALSE(input::ParseButton("Banana").has_value());
    EXPECT_FALSE(input::ParseButton("PadBanana").has_value());
    EXPECT_FALSE(input::ParseButton("Key0").has_value());
    EXPECT_EQ(input::DeviceOf(MouseButton::Left), Device::KeyboardMouse);
    EXPECT_EQ(input::DeviceOf(GamepadButton::North), Device::Gamepad);
}

TEST(BindingsTest, TheFirstPressOfTheDeviceAskedForIsTheOneBound) {
    input::InputState state;
    EXPECT_FALSE(input::FirstPressed(state, Device::KeyboardMouse).has_value());
    state.Process(platform::GamepadConnected{kPad});
    state.Process(platform::GamepadButtonPressed{kPad, GamepadButton::West});
    state.Process(platform::MouseButtonPressed{kWindow, MouseButton::X1, {}});
    EXPECT_EQ(input::FirstPressed(state, Device::KeyboardMouse), ActionMap::Button(MouseButton::X1));
    EXPECT_EQ(input::FirstPressed(state, Device::Gamepad), ActionMap::Button(GamepadButton::West));
    state.Process(platform::KeyPressed{kWindow, Key::G, false});
    EXPECT_EQ(input::FirstPressed(state, Device::KeyboardMouse), ActionMap::Button(Key::G));
    // Held is not pressed: a press binds once.
    state.BeginFrame();
    EXPECT_FALSE(input::FirstPressed(state, Device::KeyboardMouse).has_value());
    EXPECT_FALSE(input::FirstPressed(state, Device::Gamepad).has_value());
}

TEST(RumbleTest, PulsesFadeAddUpAndStopAtOne) {
    input::RumbleMixer rumble;
    EXPECT_FALSE(rumble.Update(0.016f).has_value());
    rumble.Pulse(0.8f, 0.2f, 1.0f);
    const std::optional<input::RumbleLevel> first = rumble.Update(0.0f);
    ASSERT_TRUE(first.has_value());
    EXPECT_FLOAT_EQ(first->low, 0.8f);
    (void)rumble.Update(0.5f);
    EXPECT_NEAR(rumble.Current().low, 0.4f, 1e-5f);
    rumble.Pulse(0.9f, 0.9f, 1.0f);
    (void)rumble.Update(0.0f);
    EXPECT_FLOAT_EQ(rumble.Current().low, 1.0f);
    (void)rumble.Update(2.0f);
    EXPECT_TRUE(rumble.Current().IsStill());
}

TEST(RumbleTest, ALevelIsSentWhenItChangesEnoughAndBeforeItLapses) {
    input::RumbleMixer rumble;
    rumble.Hold({.low = 0.5f});
    ASSERT_TRUE(rumble.Update(0.01f).has_value());
    // The same level, or nearly: nothing to send until half its time on the pad is gone.
    rumble.Hold({.low = 0.51f});
    EXPECT_FALSE(rumble.Update(0.1f).has_value());
    EXPECT_FALSE(rumble.Update(0.1f).has_value());
    EXPECT_TRUE(rumble.Update(input::RumbleMixer::kSentSeconds * 0.5f).has_value());
    rumble.Hold({.low = 0.7f});
    EXPECT_TRUE(rumble.Update(0.01f).has_value());
    // Stillness is sent once, exactly.
    rumble.Stop();
    const std::optional<input::RumbleLevel> still = rumble.Update(0.01f);
    ASSERT_TRUE(still.has_value());
    EXPECT_TRUE(still->IsStill());
    EXPECT_FALSE(rumble.Update(5.0f).has_value());
}
