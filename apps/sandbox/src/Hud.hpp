// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/ui/Canvas.hpp>

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
};

[[nodiscard]] daidalos::core::Result<HudAssets> LoadHud(daidalos::scene::AssetLibrary& assets);

/// The legacy demo's layout: health and armor at the bottom left, ammo at the bottom right, the
/// flashlight icon at the top right and the crosshair in the middle, with the last message above it.
void DrawHud(daidalos::ui::Canvas& canvas, const HudAssets& hud, const Player& player);

/// The legacy loading screen: "LOADING..." in the middle of a black screen, and a gear turning in the
/// bottom-right corner; over the current level, darkened, when there is one. seconds is how long it has
/// been loading, which turns the gear. There is no progress bar: a level is one file, loaded at once.
void DrawLoading(daidalos::ui::Canvas& canvas, const HudAssets& hud, bool coverScreen, float seconds);

} // namespace sandbox
