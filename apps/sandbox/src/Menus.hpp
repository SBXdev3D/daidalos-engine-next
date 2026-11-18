// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/ui/Canvas.hpp>
#include <daidalos/ui/Context.hpp>

#include <cstdint>

#include "Hud.hpp"

namespace sandbox {

/// The screens drawn over the game. None is playing.
enum class Screen : std::uint8_t { None, Main, Pause, Options, Calibration, Dead, Complete };

/// The pages of the options screen, as tabs.
enum class OptionsPage : std::uint8_t { Video, Sound, Controls, Accessibility };

/// What the player picked on a screen this frame.
enum class MenuChoice : std::uint8_t { None, Play, Resume, Restart, Options, Calibrate, Back, Quit };

/// The look of the game's buttons, sliders and choices.
[[nodiscard]] daidalos::ui::WidgetStyle MenuStyle(const HudAssets& hud);

[[nodiscard]] MenuChoice DrawMainMenu(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud);
[[nodiscard]] MenuChoice DrawPauseMenu(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud);
/// Kills out of the level's zombies.
[[nodiscard]] MenuChoice DrawDeathScreen(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud,
                                         const Player& player);
/// Every zombie is dead: how long it took, in seconds of play.
[[nodiscard]] MenuChoice DrawCompleteScreen(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas,
                                            const HudAssets& hud, const Player& player, float seconds);

/// The engine's settings that a player changes, applied at once with Application::ChangeSetting.
/// Returns Back when done; the caller saves them then.
[[nodiscard]] MenuChoice DrawOptions(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud,
                                     daidalos::app::Application& app, OptionsPage& page, bool& changed);

/// The brightness calibration (ADR-0048): a symbol a little brighter than black, as the display transform
/// shows a dark surface at the chosen brightness, and the Brightness slider. Shown at the first start.
/// Returns Back when done.
[[nodiscard]] MenuChoice DrawCalibration(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud,
                                         daidalos::app::Application& app, bool& changed);

} // namespace sandbox
