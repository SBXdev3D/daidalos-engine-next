// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/CVarRegistry.hpp>
#include <daidalos/input/ActionMap.hpp>
#include <daidalos/input/Bindings.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sandbox {

/// The game's actions, which the controls and fixed bindings trigger.
enum Action : daidalos::input::Action {
    MoveForward,
    MoveRight,
    Jump,
    Crouch,
    Run,
    Interact,
    ToggleFlashlight,
    Restart,
    ToggleMouse,
    /// Pauses the game, or goes back from a menu.
    Menu,
    Fire,
    /// The gamepad's trigger, which fires past half of its travel.
    FireTrigger,
    Reload,
    NextWeapon,
    SelectHandgun,
    SelectShotgun,
    /// Saves the game, and loads the last save (ADR-0072).
    QuickSave,
    QuickLoad,
    /// Goes back from a menu, as Menu does, but only there: the gamepad's East.
    MenuBack,
};

/// What a control does to its action: presses it, or pushes its axis one way.
enum class ControlRole : std::uint8_t { Button, Positive, Negative };

/// A control the player can bind (ADR-0076), to a key or mouse button and to a gamepad button. Each binding
/// is a setting, bind.key.<id> and bind.pad.<id>, holding a button's name (input::ButtonName), or nothing
/// for none; its text key is options.control.<id>. The sticks, the fire trigger, Escape, Tab and the menu
/// buttons stay where they are.
struct Control {
    std::string_view id;
    daidalos::input::Action action = 0;
    ControlRole role = ControlRole::Button;
    std::string_view key;
    std::string_view pad;
};

/// The controls, in the order the options list them.
[[nodiscard]] std::span<const Control> Controls();

/// The setting holding a control's binding on a device.
[[nodiscard]] std::string BindingSetting(const Control& control, daidalos::input::Device device);

/// Registers the controls' settings, with their defaults.
void RegisterControls(daidalos::core::CVarRegistry& cvars);

/// What a control is bound to on a device: its setting, or its default when the setting does not name a
/// button. Nothing when unbound.
[[nodiscard]] std::optional<daidalos::input::ActionMap::Button>
BoundButton(const daidalos::core::CVarRegistry& cvars, const Control& control, daidalos::input::Device device);

/// Whether a button cannot be bound, because the game keeps it: Escape, Tab, F5 and the console's key; the
/// gamepads' Start, Back and Guide.
[[nodiscard]] bool IsReserved(const daidalos::input::ActionMap::Button& button);

/// The settings that bind a control to a button, as name and value: its own, and, when another control of
/// the device has that button, that one's, which takes the button the control had. Nothing for a reserved
/// button, or one the control already has.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> Rebind(const daidalos::core::CVarRegistry& cvars,
                                                                      const Control& control,
                                                                      const daidalos::input::ActionMap::Button& button);

/// The game's actions, from the controls' settings and the fixed bindings.
[[nodiscard]] daidalos::input::ActionMap MakeActions(const daidalos::core::CVarRegistry& cvars);

/// A button's name for a menu, short enough for a narrow column: "Space", "LMB", "A", "LB", "D-Up".
[[nodiscard]] std::string ShortName(const daidalos::input::ActionMap::Button& button);

} // namespace sandbox
