// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "Menus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

using namespace daidalos;

namespace sandbox {

namespace {

constexpr float kButtonWidth = 520.0f;
constexpr float kButtonHeight = 68.0f;
constexpr float kRowWidth = 920.0f;
constexpr float kRowHeight = 54.0f;
constexpr float kGap = 12.0f;
/// The red of the loading gear, brighter for text.
constexpr glm::vec4 kTitleRed{0.85f, 0.12f, 0.1f, 1.0f};
constexpr glm::vec4 kShadow{0.0f, 0.0f, 0.0f, 0.7f};

void Darken(ui::Canvas& canvas, float alpha) {
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, alpha});
}

void Title(ui::Canvas& canvas, const HudAssets& hud, std::string_view text, float y, float size,
           const glm::vec4& color = kTitleRed) {
    canvas.DrawText(*hud.boldFont, text, {canvas.Size().x * 0.5f, y},
                    {.size = size,
                     .color = color,
                     .horizontal = ui::Align::Center,
                     .vertical = ui::Align::Center,
                     .shadowColor = kShadow,
                     .shadowOffset = {4.0f, 4.0f}});
}

void Line(ui::Canvas& canvas, const HudAssets& hud, std::string_view text, float y) {
    canvas.DrawText(*hud.font, text, {canvas.Size().x * 0.5f, y},
                    {.size = 38.0f,
                     .color = {0.9f, 0.9f, 0.9f, 1.0f},
                     .horizontal = ui::Align::Center,
                     .vertical = ui::Align::Center,
                     .shadowColor = kShadow});
}

/// A column of buttons centered across the screen, from top. Returns the index of the one used.
template <std::size_t Count>
std::optional<std::size_t> Buttons(ui::Context& ui, const ui::Canvas& canvas, float top,
                                   const std::array<std::string_view, Count>& labels) {
    std::optional<std::size_t> used;
    const float left = (canvas.Size().x - kButtonWidth) * 0.5f;
    for (std::size_t index = 0; index < Count; ++index) {
        const float y = top + (static_cast<float>(index) * (kButtonHeight + kGap));
        if (ui.Button(labels[index], {{left, y}, {kButtonWidth, kButtonHeight}})) {
            used = index;
        }
    }
    return used;
}

const core::CVarValue* Setting(app::Application& app, std::string_view name) {
    const core::CVarEntry* entry = app.CVars().Find(name);
    return entry != nullptr ? &entry->value : nullptr;
}

template <typename T>
T SettingOr(app::Application& app, std::string_view name, T fallback) {
    const core::CVarValue* value = Setting(app, name);
    const T* typed = value != nullptr ? std::get_if<T>(value) : nullptr;
    return typed != nullptr ? *typed : fallback;
}

void Change(app::Application& app, std::string_view name, std::string_view value, bool& changed) {
    if (core::Result<void> result = app.ChangeSetting(name, value); result) {
        changed = true;
    } else {
        DAI_LOG_WARNING("sandbox", "Cannot change {}: {}", name, core::Format(result.error()));
    }
}

} // namespace

ui::WidgetStyle MenuStyle(const HudAssets& hud) {
    ui::WidgetStyle style;
    style.font = hud.font;
    return style;
}

MenuChoice DrawMainMenu(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud) {
    Darken(canvas, 0.45f);
    Title(canvas, hud, "DAIDALOS", 300.0f, 150.0f);
    Line(canvas, hud, "Something is wrong in the factory.", 410.0f);
    switch (Buttons<3>(ui, canvas, 540.0f, {"New game", "Options", "Quit"}).value_or(3)) {
    case 0:
        return MenuChoice::Play;
    case 1:
        return MenuChoice::Options;
    case 2:
        return MenuChoice::Quit;
    default:
        return MenuChoice::None;
    }
}

MenuChoice DrawPauseMenu(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud) {
    Darken(canvas, 0.6f);
    Title(canvas, hud, "PAUSED", 320.0f, 110.0f, glm::vec4(1.0f));
    switch (Buttons<4>(ui, canvas, 450.0f, {"Resume", "Restart", "Options", "Quit"}).value_or(4)) {
    case 0:
        return MenuChoice::Resume;
    case 1:
        return MenuChoice::Restart;
    case 2:
        return MenuChoice::Options;
    case 3:
        return MenuChoice::Quit;
    default:
        return MenuChoice::None;
    }
}

MenuChoice DrawDeathScreen(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, const Player& player) {
    Darken(canvas, 0.5f);
    Title(canvas, hud, "YOU ARE DEAD", 330.0f, 130.0f);
    Line(canvas, hud, std::format("Zombies killed: {} of {}", player.kills, player.zombieCount), 450.0f);
    switch (Buttons<2>(ui, canvas, 560.0f, {"Try again", "Quit"}).value_or(2)) {
    case 0:
        return MenuChoice::Restart;
    case 1:
        return MenuChoice::Quit;
    default:
        return MenuChoice::None;
    }
}

MenuChoice DrawCompleteScreen(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, const Player& player,
                              float seconds) {
    Darken(canvas, 0.6f);
    Title(canvas, hud, "FACTORY CLEARED", 330.0f, 120.0f, glm::vec4(1.0f));
    const auto whole = static_cast<int>(std::max(seconds, 0.0f));
    Line(canvas, hud, std::format("All {} zombies killed in {}:{:02}", player.zombieCount, whole / 60, whole % 60),
         450.0f);
    switch (Buttons<2>(ui, canvas, 560.0f, {"Play again", "Quit"}).value_or(2)) {
    case 0:
        return MenuChoice::Restart;
    case 1:
        return MenuChoice::Quit;
    default:
        return MenuChoice::None;
    }
}

MenuChoice DrawOptions(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, app::Application& app,
                       bool& changed) {
    Darken(canvas, 0.75f);
    Title(canvas, hud, "OPTIONS", 90.0f, 90.0f, glm::vec4(1.0f));

    const float left = (canvas.Size().x - kRowWidth) * 0.5f;
    float y = 160.0f;
    const auto row = [&]() {
        const ui::Rect rect{{left, y}, {kRowWidth, kRowHeight}};
        y += kRowHeight + (kGap * 0.5f);
        return rect;
    };
    const auto section = [&](std::string_view name) {
        y += 14.0f;
        canvas.DrawText(*hud.boldFont, name, {left, y + 18.0f},
                        {.size = 30.0f, .color = kTitleRed, .vertical = ui::Align::Center, .shadowColor = kShadow});
        y += 42.0f;
    };
    const auto toggle = [&](std::string_view label, std::string_view name) {
        bool value = SettingOr(app, name, false);
        if (ui.Toggle(label, row(), value)) {
            Change(app, name, value ? "true" : "false", changed);
        }
    };
    // Shown as display(value), written to the setting as write(value).
    const auto slider = [&](std::string_view label, std::string_view name, float min, float max, float step,
                            const std::function<std::string(float)>& display,
                            const std::function<std::string(float)>& write) {
        auto value = static_cast<float>(SettingOr(app, name, static_cast<double>(min)));
        if (ui.Slider(label, row(), value, min, max, step, display(value))) {
            Change(app, name, write(value), changed);
        }
    };
    const auto percent = [](float value) { return std::format("{:.0f}%", value * 100.0f); };
    const auto twoDecimals = [](float value) { return std::format("{:.2f}", value); };

    section("VIDEO");
    toggle("Fullscreen", "window.fullscreen");
    toggle("Vertical sync", "renderer.vsync");
    constexpr std::array<std::string_view, 2> kModes{"off", "taa"};
    constexpr std::array<std::string_view, 2> kModeNames{"Off", "TAA"};
    const std::string mode = SettingOr<std::string>(app, "renderer.antialiasing", "taa");
    std::size_t modeIndex = static_cast<std::size_t>(std::find(kModes.begin(), kModes.end(), mode) - kModes.begin());
    modeIndex = modeIndex < kModes.size() ? modeIndex : 1;
    if (ui.Choice("Antialiasing", row(), modeIndex, kModeNames)) {
        Change(app, "renderer.antialiasing", kModes[modeIndex], changed);
    }
    toggle("Indirect light", "renderer.indirect_light");
    // Stops of exposure compensation (ADR-0043): one more is twice as bright.
    slider(
        "Brightness", "renderer.exposure_compensation", -1.0f, 1.0f, 0.1f,
        [](float value) { return std::format("{:+.1f} EV", value); },
        [](float value) { return std::format("{:.1f}", value); });
    slider(
        "Field of view", "view.fov", 60.0f, 120.0f, 5.0f, [](float value) { return std::format("{:.0f}°", value); },
        [](float value) { return std::format("{:.0f}", value); });

    section("SOUND");
    slider("Volume", "audio.volume", 0.0f, 1.0f, 0.05f, percent, twoDecimals);
    slider("Effects", "audio.effects", 0.0f, 1.0f, 0.05f, percent, twoDecimals);
    slider("Music", "audio.music", 0.0f, 1.0f, 0.05f, percent, twoDecimals);

    section("CONTROLS");
    // Radians per mouse unit are tiny: shown in thousandths.
    slider(
        "Mouse sensitivity", "input.mouse_sensitivity", 0.0005f, 0.008f, 0.00025f,
        [](float value) { return std::format("{:.2f}", value * 1000.0f); },
        [](float value) { return std::format("{:.5f}", value); });

    y += 24.0f;
    const float buttonLeft = (canvas.Size().x - kButtonWidth) * 0.5f;
    return ui.Button("Back", {{buttonLeft, y}, {kButtonWidth, kButtonHeight}}) ? MenuChoice::Back : MenuChoice::None;
}

} // namespace sandbox
