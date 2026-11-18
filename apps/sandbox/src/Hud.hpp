// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Text.hpp>
#include <daidalos/ui/Canvas.hpp>

#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Factory.hpp"

namespace sandbox {

/// What the heads-up display draws with, which outlives the scenes.
struct HudAssets {
    const daidalos::ui::Font* font = nullptr;
    const daidalos::ui::Font* boldFont = nullptr;
    /// The legacy hud.png: health, armor, ammo and flashlight icons.
    daidalos::rhi::TextureHandle icons;
    daidalos::rhi::TextureHandle crosshair;
    /// The legacy loading screen's gear, which turns while a level loads.
    daidalos::rhi::TextureHandle gear;
    /// What the interface says, in the player's language (ADR-0075); owned by the game.
    const daidalos::scene::TextTable* text = nullptr;
    /// The languages there is text for: their codes and their names in themselves.
    std::vector<std::pair<std::string, std::string>> languages;
};

/// The text of a key in the player's language.
[[nodiscard]] inline std::string_view T(const HudAssets& hud, std::string_view key) {
    return hud.text != nullptr ? hud.text->Get(key) : key;
}

/// A text with values put in its {} places; a translation whose places do not fit shows its key.
template <typename... Args>
[[nodiscard]] std::string Tf(const HudAssets& hud, std::string_view key, const Args&... args) {
    try {
        return std::vformat(T(hud, key), std::make_format_args(args...));
    } catch (const std::format_error&) {
        return std::string(key);
    }
}

/// How the player wants subtitles and captions (ADR-0075).
struct CaptionSettings {
    bool subtitles = true;
    bool sounds = false;
    /// Of the text, 1 by default.
    float size = 1.0f;
};

/// Subtitles and sound captions at the bottom of the screen, on a dark band. A sound's caption shows only
/// within its reach of the eye, with the side it comes from.
void DrawCaptions(daidalos::ui::Canvas& canvas, const HudAssets& hud,
                  std::span<const daidalos::scene::Caption> captions, const glm::vec3& eye, const glm::quat& rotation,
                  const CaptionSettings& settings);

[[nodiscard]] daidalos::core::Result<HudAssets> LoadHud(daidalos::scene::AssetLibrary& assets);

/// The legacy demo's layout: health and armor at the bottom left, ammo at the bottom right, the
/// flashlight icon at the top right and the crosshair in the middle, with the last message above it.
void DrawHud(daidalos::ui::Canvas& canvas, const HudAssets& hud, const Player& player);

/// The legacy loading screen: "LOADING..." in the middle of a black screen, and a gear turning in the
/// bottom-right corner; over the current level, darkened, when there is one. seconds is how long it has
/// been loading, which turns the gear. There is no progress bar: a level is one file, loaded at once.
void DrawLoading(daidalos::ui::Canvas& canvas, const HudAssets& hud, bool coverScreen, float seconds);

} // namespace sandbox
