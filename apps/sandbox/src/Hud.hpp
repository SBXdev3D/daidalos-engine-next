// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

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
};

[[nodiscard]] daidalos::core::Result<HudAssets> LoadHud(daidalos::scene::AssetLibrary& assets);

/// The legacy demo's layout: health and armor at the bottom left, ammo at the bottom right, the
/// flashlight icon at the top right and the crosshair in the middle, with the last message above it.
void DrawHud(daidalos::ui::Canvas& canvas, const HudAssets& hud, const Player& player);

/// "LOADING..." in the middle of a black screen, as the legacy demo showed; over the current level
/// instead when there is one.
void DrawLoading(daidalos::ui::Canvas& canvas, const HudAssets& hud, bool coverScreen);

} // namespace sandbox
