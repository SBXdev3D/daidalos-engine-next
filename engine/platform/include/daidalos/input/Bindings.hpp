// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/input/ActionMap.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace daidalos::input {

/// What a control is, for rebinding (ADR-0076): a keyboard key or mouse button, or a gamepad button. A
/// game's controls menu has a column for each.
enum class Device : std::uint8_t { KeyboardMouse, Gamepad };

[[nodiscard]] Device DeviceOf(const ActionMap::Button& button);

/// The name of a control in config files and the console: the Key's name ("W", "LeftShift", "F5"),
/// "Mouse" and the button's ("MouseLeft"), "Pad" and the button's ("PadSouth"). A key without a name is
/// "Key" and its number. ParseButton reads it back.
[[nodiscard]] std::string ButtonName(const ActionMap::Button& button);
/// A control from its name, ignoring case, or nothing for a name it does not know.
[[nodiscard]] std::optional<ActionMap::Button> ParseButton(std::string_view name);

/// The first control of a device pressed this frame, for a menu that waits for the player to press the
/// control to bind. Keys come first, then mouse buttons, then gamepad buttons in their order.
[[nodiscard]] std::optional<ActionMap::Button> FirstPressed(const InputState& state, Device device);

} // namespace daidalos::input
