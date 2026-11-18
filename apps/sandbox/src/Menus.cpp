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
#include <vector>

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
    Title(canvas, hud, T(hud, "menu.title"), 300.0f, 150.0f);
    Line(canvas, hud, T(hud, "menu.tagline"), 410.0f);
    switch (Buttons<3>(ui, canvas, 540.0f, {T(hud, "menu.new_game"), T(hud, "menu.options"), T(hud, "menu.quit")})
                .value_or(3)) {
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
    Title(canvas, hud, T(hud, "menu.paused"), 320.0f, 110.0f, glm::vec4(1.0f));
    switch (Buttons<4>(ui, canvas, 450.0f,
                       {T(hud, "menu.resume"), T(hud, "menu.restart"), T(hud, "menu.options"), T(hud, "menu.quit")})
                .value_or(4)) {
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
    Title(canvas, hud, T(hud, "menu.dead"), 330.0f, 130.0f);
    Line(canvas, hud, Tf(hud, "menu.killed", player.kills, player.zombieCount), 450.0f);
    switch (Buttons<2>(ui, canvas, 560.0f, {T(hud, "menu.try_again"), T(hud, "menu.quit")}).value_or(2)) {
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
    Title(canvas, hud, T(hud, "menu.cleared"), 330.0f, 120.0f, glm::vec4(1.0f));
    const auto whole = static_cast<int>(std::max(seconds, 0.0f));
    Line(canvas, hud, Tf(hud, "menu.cleared_time", player.zombieCount, whole / 60, whole % 60), 450.0f);
    switch (Buttons<2>(ui, canvas, 560.0f, {T(hud, "menu.play_again"), T(hud, "menu.quit")}).value_or(2)) {
    case 0:
        return MenuChoice::Restart;
    case 1:
        return MenuChoice::Quit;
    default:
        return MenuChoice::None;
    }
}

MenuChoice DrawOptions(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, app::Application& app,
                       OptionsPage& page, bool& changed) {
    Darken(canvas, 0.75f);
    Title(canvas, hud, T(hud, "options.title"), 90.0f, 90.0f, glm::vec4(1.0f));

    const float left = (canvas.Size().x - kRowWidth) * 0.5f;
    // The pages, as a row of tabs; the current one is underlined.
    constexpr std::array<std::string_view, 4> kPages{"options.video", "options.sound", "options.controls",
                                                     "options.accessibility"};
    constexpr float kTabGap = 10.0f;
    const float tabWidth = (kRowWidth - (kTabGap * 3.0f)) / 4.0f;
    for (std::size_t index = 0; index < kPages.size(); ++index) {
        const ui::Rect tab{{left + (static_cast<float>(index) * (tabWidth + kTabGap)), 150.0f}, {tabWidth, 56.0f}};
        if (ui.Button(T(hud, kPages[index]), tab)) {
            page = static_cast<OptionsPage>(index);
        }
        if (static_cast<std::size_t>(page) == index) {
            canvas.FillRect({{tab.position.x, tab.position.y + tab.size.y}, {tab.size.x, 4.0f}}, kTitleRed);
        }
    }

    float y = 240.0f;
    constexpr float kOptionHeight = 54.0f;
    const auto row = [&]() {
        const ui::Rect rect{{left, y}, {kRowWidth, kOptionHeight}};
        y += kOptionHeight + (kGap * 0.5f);
        return rect;
    };
    const auto toggle = [&](std::string_view label, std::string_view name, bool fallback = false) {
        bool value = SettingOr(app, name, fallback);
        if (ui.Toggle(T(hud, label), row(), value)) {
            Change(app, name, value ? "true" : "false", changed);
        }
    };
    // Shown as display(value), written to the setting as write(value).
    const auto slider = [&](std::string_view label, std::string_view name, float min, float max, float step,
                            const std::function<std::string(float)>& display,
                            const std::function<std::string(float)>& write, double fallback) {
        auto value = static_cast<float>(SettingOr(app, name, fallback));
        if (ui.Slider(T(hud, label), row(), value, min, max, step, display(value))) {
            Change(app, name, write(value), changed);
        }
    };
    // A choice among named values of a text setting; labels are text keys.
    const auto choice = [&](std::string_view label, std::string_view name, std::span<const std::string_view> values,
                            std::span<const std::string_view> labels, std::string_view fallback) {
        const std::string current = SettingOr<std::string>(app, name, std::string(fallback));
        std::size_t index = static_cast<std::size_t>(std::find(values.begin(), values.end(), current) - values.begin());
        index = index < values.size() ? index : 0;
        std::vector<std::string_view> shown;
        for (const std::string_view key : labels) {
            shown.push_back(T(hud, key));
        }
        if (ui.Choice(T(hud, label), row(), index, shown)) {
            Change(app, name, values[index], changed);
        }
    };
    const auto percent = [](float value) { return std::format("{:.0f}%", value * 100.0f); };
    const auto twoDecimals = [](float value) { return std::format("{:.2f}", value); };

    switch (page) {
    case OptionsPage::Video: {
        toggle("options.fullscreen", "window.fullscreen");
        toggle("options.vsync", "renderer.vsync", true);
        constexpr std::array<std::string_view, 2> kModes{"taa", "off"};
        constexpr std::array<std::string_view, 2> kModeNames{"options.antialiasing.taa", "options.antialiasing.off"};
        choice("options.antialiasing", "renderer.antialiasing", kModes, kModeNames, "taa");
        // The render resolution (ADR-0049): dynamic, up to the window's and down to half of it as the GPU
        // needs, or fixed. Temporal antialiasing upscales it.
        constexpr std::array<double, 5> kScales{1.0, 1.0, 0.67, 0.58, 0.5};
        constexpr std::array<std::string_view, 5> kScaleNames{
            "options.resolution.dynamic", "options.resolution.native", "options.resolution.quality",
            "options.resolution.balanced", "options.resolution.performance"};
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
        std::vector<std::string_view> scaleNames;
        for (const std::string_view key : kScaleNames) {
            scaleNames.push_back(T(hud, key));
        }
        if (ui.Choice(T(hud, "options.resolution"), row(), scaleIndex, scaleNames)) {
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
        if (ui.Choice(T(hud, "options.frame_rate"), row(), frameRateIndex, kFrameRateNames)) {
            Change(app, "renderer.target_fps", std::to_string(kFrameRates[frameRateIndex]), changed);
        }
        // The quality preset (ADR-0052); Custom when a setting it decides was set apart from it.
        const std::array<std::string_view, 5> qualityNames{
            T(hud, "options.quality.low"), T(hud, "options.quality.medium"), T(hud, "options.quality.high"),
            T(hud, "options.quality.ultra"), T(hud, "options.quality.custom")};
        const bool custom = app.QualityIsCustom();
        std::size_t qualityIndex = custom ? 4 : static_cast<std::size_t>(app.CurrentQuality());
        if (ui.Choice(T(hud, "options.quality"), row(), qualityIndex,
                      std::span<const std::string_view>(qualityNames).first(custom ? 5 : 4)) &&
            qualityIndex < app::kQualities.size()) {
            if (core::Result<void> chosen = app.ChooseQuality(app::kQualities[qualityIndex]); chosen) {
                changed = true;
            } else {
                DAI_LOG_WARNING("sandbox", "Cannot change the quality: {}", core::Format(chosen.error()));
            }
        }
        // Stops of exposure compensation (ADR-0043): one more is twice as bright.
        slider(
            "options.brightness", "renderer.exposure_compensation", -1.0f, 1.0f, 0.1f,
            [](float value) { return std::format("{:+.1f} EV", value); },
            [](float value) { return std::format("{:.1f}", value); }, 0.0);
        slider(
            "options.fov", "view.fov", 60.0f, 120.0f, 5.0f, [](float value) { return std::format("{:.0f}°", value); },
            [](float value) { return std::format("{:.0f}", value); }, 90.0);
        break;
    }
    case OptionsPage::Sound:
        slider("options.volume", "audio.volume", 0.0f, 1.0f, 0.05f, percent, twoDecimals, 1.0);
        slider("options.effects", "audio.effects", 0.0f, 1.0f, 0.05f, percent, twoDecimals, 1.0);
        slider("options.music", "audio.music", 0.0f, 1.0f, 0.05f, percent, twoDecimals, 1.0);
        toggle("options.subtitles", "game.subtitles", true);
        toggle("options.captions", "game.captions", false);
        break;
    case OptionsPage::Controls:
        // Radians per mouse unit are tiny: shown in thousandths.
        slider(
            "options.mouse_sensitivity", "input.mouse_sensitivity", 0.0005f, 0.008f, 0.00025f,
            [](float value) { return std::format("{:.2f}", value * 1000.0f); },
            [](float value) { return std::format("{:.5f}", value); }, 0.0025);
        break;
    case OptionsPage::Accessibility: {
        // The language, among those there is text for, by its name in itself (ADR-0075).
        if (!hud.languages.empty()) {
            const std::string language = SettingOr<std::string>(app, "game.language", "en");
            std::size_t index = 0;
            std::vector<std::string_view> names;
            for (std::size_t at = 0; at < hud.languages.size(); ++at) {
                names.push_back(hud.languages[at].second);
                if (hud.languages[at].first == language) {
                    index = at;
                }
            }
            if (ui.Choice(T(hud, "options.language"), row(), index, names)) {
                Change(app, "game.language", hud.languages[index].first, changed);
            }
        }
        toggle("options.subtitles", "game.subtitles", true);
        toggle("options.captions", "game.captions", false);
        slider("options.text_size", "game.text_size", 0.75f, 2.0f, 0.25f, percent, twoDecimals, 1.0);
        constexpr std::array<std::string_view, 4> kVisions{"normal", "protanopia", "deuteranopia", "tritanopia"};
        constexpr std::array<std::string_view, 4> kVisionNames{
            "options.color_vision.normal", "options.color_vision.protanopia", "options.color_vision.deuteranopia",
            "options.color_vision.tritanopia"};
        choice("options.color_vision", "renderer.color_vision", kVisions, kVisionNames, "normal");
        slider("options.camera_shake", "view.camera_shake", 0.0f, 1.0f, 0.25f, percent, twoDecimals, 1.0);
        slider("options.motion_blur", "renderer.motion_blur", 0.0f, 1.0f, 0.25f, percent, twoDecimals, 0.0);
        slider("options.film_grain", "renderer.film_grain", 0.0f, 1.0f, 0.05f, percent, twoDecimals, 0.35);
        slider("options.chromatic_aberration", "renderer.chromatic_aberration", 0.0f, 1.0f, 0.05f, percent, twoDecimals,
               0.0);
        slider("options.lens_dirt", "renderer.lens_dirt", 0.0f, 1.0f, 0.05f, percent, twoDecimals, 0.5);
        break;
    }
    }

    constexpr float kHalfButton = (kButtonWidth - kGap) * 0.5f;
    const float buttonLeft = (canvas.Size().x - kButtonWidth) * 0.5f;
    const float buttons = std::max(y + 16.0f, 900.0f);
    if (ui.Button(T(hud, "options.calibrate"), {{buttonLeft, buttons}, {kHalfButton, kButtonHeight}})) {
        return MenuChoice::Calibrate;
    }
    return ui.Button(T(hud, "options.back"), {{buttonLeft + kHalfButton + kGap, buttons}, {kHalfButton, kButtonHeight}})
               ? MenuChoice::Back
               : MenuChoice::None;
}

MenuChoice DrawCalibration(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, app::Application& app,
                           bool& changed) {
    // A dark surface, as exposed light: about 1.5% of middle grey. At a Brightness of 0 on a display at its
    // usual settings, the display transform shows it at about 0.4% of white, sRGB 12 of 255.
    constexpr float kDarkSurface = 0.008f;
    constexpr float kSymbolSize = 300.0f;
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, 1.0f});
    Title(canvas, hud, T(hud, "options.brightness"), 170.0f, 90.0f, glm::vec4(1.0f));
    Line(canvas, hud, T(hud, "options.calibration"), 270.0f);

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
    if (ui.Slider(T(hud, "options.brightness"), {{left, 760.0f}, {kRowWidth, kRowHeight}}, value, -1.0f, 1.0f, 0.1f,
                  std::format("{:+.1f} EV", value))) {
        Change(app, "renderer.exposure_compensation", std::format("{:.1f}", value), changed);
    }
    const float buttonLeft = (canvas.Size().x - kButtonWidth) * 0.5f;
    return ui.Button(T(hud, "options.done"), {{buttonLeft, 860.0f}, {kButtonWidth, kButtonHeight}}) ? MenuChoice::Back
                                                                                                    : MenuChoice::None;
}

} // namespace sandbox
