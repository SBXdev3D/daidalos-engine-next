// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Controls.hpp"

#include <daidalos/platform/Platform.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <variant>

namespace sandbox {

namespace {

using namespace daidalos;
using input::ActionMap;
using input::Device;
using platform::GamepadAxis;
using platform::GamepadButton;
using platform::Key;
using platform::MouseButton;

// clang-format off
constexpr std::array kControls{
    Control{"move_forward", MoveForward,      ControlRole::Positive, "W",         ""},
    Control{"move_back",    MoveForward,      ControlRole::Negative, "S",         ""},
    Control{"move_left",    MoveRight,        ControlRole::Negative, "A",         ""},
    Control{"move_right",   MoveRight,        ControlRole::Positive, "D",         ""},
    Control{"jump",         Jump,             ControlRole::Button,   "Space",     "PadSouth"},
    Control{"crouch",       Crouch,           ControlRole::Button,   "C",         "PadEast"},
    Control{"run",          Run,              ControlRole::Button,   "LeftShift", "PadLeftShoulder"},
    Control{"interact",     Interact,         ControlRole::Button,   "E",         "PadWest"},
    Control{"flashlight",   ToggleFlashlight, ControlRole::Button,   "F",         "PadNorth"},
    Control{"fire",         Fire,             ControlRole::Button,   "MouseLeft", ""},
    Control{"reload",       Reload,           ControlRole::Button,   "R",         "PadRightShoulder"},
    Control{"next_weapon",  NextWeapon,       ControlRole::Button,   "Q",         "PadDpadUp"},
    Control{"handgun",      SelectHandgun,    ControlRole::Button,   "Digit1",    "PadDpadLeft"},
    Control{"shotgun",      SelectShotgun,    ControlRole::Button,   "Digit2",    "PadDpadRight"},
    Control{"quick_save",   QuickSave,        ControlRole::Button,   "F6",        ""},
    Control{"quick_load",   QuickLoad,        ControlRole::Button,   "F9",        ""},
};
// clang-format on

std::string_view DefaultOf(const Control& control, Device device) {
    return device == Device::KeyboardMouse ? control.key : control.pad;
}

} // namespace

std::span<const Control> Controls() {
    return kControls;
}

std::string BindingSetting(const Control& control, Device device) {
    return std::format("bind.{}.{}", device == Device::KeyboardMouse ? "key" : "pad", control.id);
}

void RegisterControls(core::CVarRegistry& cvars) {
    for (const Control& control : kControls) {
        for (const Device device : {Device::KeyboardMouse, Device::Gamepad}) {
            if (cvars.Find(BindingSetting(control, device)) == nullptr) {
                (void)cvars.Register<std::string>(BindingSetting(control, device),
                                                  std::string(DefaultOf(control, device)),
                                                  "A control's binding, a button's name; empty for none (ADR-0076)");
            }
        }
    }
}

std::optional<ActionMap::Button> BoundButton(const core::CVarRegistry& cvars, const Control& control, Device device) {
    std::string_view name = DefaultOf(control, device);
    const core::CVarEntry* entry = cvars.Find(BindingSetting(control, device));
    if (const std::string* value = entry != nullptr ? std::get_if<std::string>(&entry->value) : nullptr) {
        if (value->empty()) {
            return std::nullopt;
        }
        // A name that does not parse, or names a button of the other device, keeps the default.
        if (const std::optional<ActionMap::Button> button = input::ParseButton(*value);
            button.has_value() && input::DeviceOf(*button) == device && !IsReserved(*button)) {
            return button;
        }
    }
    if (name.empty()) {
        return std::nullopt;
    }
    return input::ParseButton(name);
}

bool IsReserved(const ActionMap::Button& button) {
    if (const auto* key = std::get_if<Key>(&button)) {
        return *key == Key::Escape || *key == Key::Tab || *key == Key::F5 || *key == Key::Grave || *key == Key::Unknown;
    }
    if (const auto* pad = std::get_if<GamepadButton>(&button)) {
        return *pad == GamepadButton::Start || *pad == GamepadButton::Back || *pad == GamepadButton::Guide;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> Rebind(const core::CVarRegistry& cvars, const Control& control,
                                                        const ActionMap::Button& button) {
    std::vector<std::pair<std::string, std::string>> changes;
    if (IsReserved(button)) {
        return changes;
    }
    const Device device = input::DeviceOf(button);
    const std::optional<ActionMap::Button> had = BoundButton(cvars, control, device);
    if (had == button) {
        return changes;
    }
    changes.emplace_back(BindingSetting(control, device), input::ButtonName(button));
    for (const Control& other : kControls) {
        if (&other != &control && other.id != control.id && BoundButton(cvars, other, device) == button) {
            changes.emplace_back(BindingSetting(other, device), had.has_value() ? input::ButtonName(*had) : "");
        }
    }
    return changes;
}

ActionMap MakeActions(const core::CVarRegistry& cvars) {
    ActionMap actions;
    // The movement keys come in pairs, each side its own control.
    const auto pairOf = [&](Action action) {
        std::optional<ActionMap::Button> positive;
        std::optional<ActionMap::Button> negative;
        for (const Control& control : kControls) {
            if (control.action == action && control.role == ControlRole::Positive) {
                positive = BoundButton(cvars, control, Device::KeyboardMouse);
            } else if (control.action == action && control.role == ControlRole::Negative) {
                negative = BoundButton(cvars, control, Device::KeyboardMouse);
            }
        }
        // A side left unbound is a key never pressed.
        actions.BindAxis(action, positive.value_or(Key::Unknown), negative.value_or(Key::Unknown));
    };
    pairOf(MoveForward);
    pairOf(MoveRight);
    for (const Control& control : kControls) {
        if (control.role != ControlRole::Button) {
            continue;
        }
        for (const Device device : {Device::KeyboardMouse, Device::Gamepad}) {
            if (const std::optional<ActionMap::Button> button = BoundButton(cvars, control, device)) {
                actions.Bind(control.action, *button);
            }
        }
    }
    // Fixed: the sticks and the trigger, the menu, the mouse's capture and the restart.
    actions.BindAxis(MoveForward, GamepadAxis::LeftY);
    actions.BindAxis(MoveRight, GamepadAxis::LeftX);
    actions.BindAxis(FireTrigger, GamepadAxis::RightTrigger);
    actions.Bind(Menu, Key::Escape);
    actions.Bind(Menu, GamepadButton::Start);
    actions.Bind(Menu, GamepadButton::Back);
    actions.Bind(MenuBack, GamepadButton::East);
    actions.Bind(ToggleMouse, Key::Tab);
    actions.Bind(Restart, Key::F5);
    return actions;
}

std::string ShortName(const ActionMap::Button& button) {
    if (const auto* mouse = std::get_if<MouseButton>(&button)) {
        constexpr std::array<std::string_view, platform::kMouseButtonCount> kMouse{"LMB", "MMB", "RMB", "M4", "M5"};
        return std::string(kMouse.at(static_cast<std::size_t>(*mouse)));
    }
    if (const auto* pad = std::get_if<GamepadButton>(&button)) {
        // Named as on the most common pads; the game's own words for them would be a translation each.
        constexpr std::array<std::string_view, platform::kGamepadButtonCount> kPad{
            "A",  "B",  "X",  "Y",    "Back",   "Guide",  "Start",  "LS",
            "RS", "LB", "RB", "D-Up", "D-Down", "D-Left", "D-Right"};
        return std::string(kPad.at(static_cast<std::size_t>(*pad)));
    }
    const Key key = std::get<Key>(button);
    switch (key) {
    case Key::LeftShift:
        return "LShift";
    case Key::RightShift:
        return "RShift";
    case Key::LeftCtrl:
        return "LCtrl";
    case Key::RightCtrl:
        return "RCtrl";
    case Key::LeftAlt:
        return "LAlt";
    case Key::RightAlt:
        return "RAlt";
    default:
        break;
    }
    // A letter or sign as the keyboard's layout prints it, so that AZERTY shows Z where QWERTY has W. Digits
    // keep theirs: AZERTY prints & on 1, but players call it 1.
    const bool digit = key >= Key::Digit1 && key <= Key::Digit0;
    if (const std::string label = digit ? std::string() : platform::KeyLabel(key);
        !label.empty() && label.size() <= 2) {
        return label;
    }
    std::string name = input::ButtonName(button);
    if (name.starts_with("Digit")) {
        name.erase(0, 5);
    }
    return name;
}

} // namespace sandbox
