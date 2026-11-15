// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Menus.hpp"

#include <daidalos/renderer/DisplayTransform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <span>
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

/// Linear to sRGB, which canvas colors are in.
float LinearToSrgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? value * 12.92f : (1.055f * std::pow(value, 1.0f / 2.4f)) - 0.055f;
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
    // Rows a little shorter than the other menus', so that twelve fit a 1080-pixel-high canvas.
    constexpr float kOptionHeight = 50.0f;
    const auto row = [&]() {
        const ui::Rect rect{{left, y}, {kRowWidth, kOptionHeight}};
        y += kOptionHeight + (kGap * 0.35f);
        return rect;
    };
    const auto section = [&](std::string_view name) {
        y += 10.0f;
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
    // The render resolution (ADR-0049): dynamic, up to the window's and down to half of it as the GPU needs,
    // or fixed. Temporal antialiasing upscales it.
    constexpr std::array<double, 5> kScales{1.0, 1.0, 0.67, 0.58, 0.5};
    constexpr std::array<std::string_view, 5> kScaleNames{"Dynamic", "Native", "Quality", "Balanced", "Performance"};
    const bool dynamic = SettingOr(app, "renderer.dynamic_resolution", true);
    const double scale = SettingOr(app, "renderer.render_scale", 1.0);
    std::size_t scaleIndex = 0;
    if (!dynamic) {
        scaleIndex = 1;
        for (std::size_t index = 1; index < kScales.size(); ++index) {
            if (std::abs(kScales[index] - scale) < std::abs(kScales[scaleIndex] - scale)) {
                scaleIndex = index;
            }
        }
    }
    if (ui.Choice("Resolution", row(), scaleIndex, kScaleNames)) {
        Change(app, "renderer.dynamic_resolution", scaleIndex == 0 ? "true" : "false", changed);
        if (scaleIndex == 0) {
            // Dynamic resolution moves within what the quality preset allows.
            app.ResetSetting("renderer.render_scale");
        } else {
            Change(app, "renderer.render_scale", std::format("{:.2f}", kScales[scaleIndex]), changed);
        }
    }
    // The frame rate dynamic resolution holds; the nearest listed one is shown for a value set elsewhere.
    constexpr std::array<std::int64_t, 7> kFrameRates{30, 60, 90, 120, 144, 165, 240};
    constexpr std::array<std::string_view, 7> kFrameRateNames{"30 fps",  "60 fps",  "90 fps", "120 fps",
                                                              "144 fps", "165 fps", "240 fps"};
    const std::int64_t frameRate = SettingOr<std::int64_t>(app, "renderer.target_fps", 60);
    std::size_t frameRateIndex = 0;
    for (std::size_t index = 1; index < kFrameRates.size(); ++index) {
        if (std::abs(kFrameRates[index] - frameRate) < std::abs(kFrameRates[frameRateIndex] - frameRate)) {
            frameRateIndex = index;
        }
    }
    if (ui.Choice("Target frame rate", row(), frameRateIndex, kFrameRateNames)) {
        Change(app, "renderer.target_fps", std::to_string(kFrameRates[frameRateIndex]), changed);
    }
    // The quality preset (ADR-0052); Custom when a setting it decides was set apart from it.
    constexpr std::array<std::string_view, 5> kQualityNames{"Low", "Medium", "High", "Ultra", "Custom"};
    const bool custom = app.QualityIsCustom();
    std::size_t qualityIndex = custom ? 4 : static_cast<std::size_t>(app.CurrentQuality());
    if (ui.Choice("Quality", row(), qualityIndex,
                  std::span<const std::string_view>(kQualityNames).first(custom ? 5 : 4)) &&
        qualityIndex < app::kQualities.size()) {
        if (core::Result<void> chosen = app.ChooseQuality(app::kQualities[qualityIndex]); chosen) {
            changed = true;
        } else {
            DAI_LOG_WARNING("sandbox", "Cannot change the quality: {}", core::Format(chosen.error()));
        }
    }
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

    y += 16.0f;
    constexpr float kHalfButton = (kButtonWidth - kGap) * 0.5f;
    const float buttonLeft = (canvas.Size().x - kButtonWidth) * 0.5f;
    if (ui.Button("Calibrate", {{buttonLeft, y}, {kHalfButton, kButtonHeight}})) {
        return MenuChoice::Calibrate;
    }
    return ui.Button("Back", {{buttonLeft + kHalfButton + kGap, y}, {kHalfButton, kButtonHeight}}) ? MenuChoice::Back
                                                                                                   : MenuChoice::None;
}

MenuChoice DrawCalibration(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, app::Application& app,
                           bool& changed) {
    // A dark surface, as exposed light: about 1.5% of middle grey. At a Brightness of 0 on a display at its
    // usual settings, the display transform shows it at about 0.4% of white, sRGB 12 of 255.
    constexpr float kDarkSurface = 0.008f;
    constexpr float kSymbolSize = 300.0f;
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, 1.0f});
    Title(canvas, hud, "BRIGHTNESS", 170.0f, 90.0f, glm::vec4(1.0f));
    Line(canvas, hud, "Move the slider until the gear is barely visible.", 270.0f);

    const auto stops = static_cast<float>(SettingOr(app, "renderer.exposure_compensation", 0.0));
    const renderer::ToneMapper toneMapper = SettingOr<std::string>(app, "renderer.tonemap", "agx") == "neutral"
                                                ? renderer::ToneMapper::Neutral
                                                : renderer::ToneMapper::AgX;
    const glm::vec3 shown = renderer::DisplayColor(glm::vec3(kDarkSurface * std::exp2(stops)), toneMapper);
    const glm::vec4 symbol{LinearToSrgb(shown.r), LinearToSrgb(shown.g), LinearToSrgb(shown.b), 1.0f};
    const glm::vec2 center{canvas.Size().x * 0.5f, 520.0f};
    canvas.DrawImage(hud.gear, {center - (kSymbolSize * 0.5f), {kSymbolSize, kSymbolSize}}, {.color = symbol});

    float value = stops;
    const float left = (canvas.Size().x - kRowWidth) * 0.5f;
    if (ui.Slider("Brightness", {{left, 760.0f}, {kRowWidth, kRowHeight}}, value, -1.0f, 1.0f, 0.1f,
                  std::format("{:+.1f} EV", value))) {
        Change(app, "renderer.exposure_compensation", std::format("{:.1f}", value), changed);
    }
    const float buttonLeft = (canvas.Size().x - kButtonWidth) * 0.5f;
    return ui.Button("Done", {{buttonLeft, 860.0f}, {kButtonWidth, kButtonHeight}}) ? MenuChoice::Back
                                                                                    : MenuChoice::None;
}

} // namespace sandbox
