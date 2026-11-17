// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Hud.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <format>
#include <string>

using namespace daidalos;

namespace sandbox {

namespace {

// Canvas units, from the legacy CHudView, which was laid out for 1080 pixels.
constexpr float kIconSize = 32.0f;
constexpr float kMargin = 40.0f;
constexpr float kLabelSize = 40.0f;
constexpr float kMessageSize = 34.0f;
constexpr float kCrosshairSize = 32.0f;
constexpr glm::vec4 kShadow{0.0f, 0.0f, 0.0f, 0.6f};

// Places of the icons in hud.png.
constexpr ui::Rect kHealthIcon{{0.0f, 0.281f}, {0.125f, 0.125f}};
constexpr ui::Rect kArmorIcon{{0.0f, 0.141f}, {0.125f, 0.125f}};
constexpr ui::Rect kAmmoIcon{{0.0f, 0.0f}, {0.125f, 0.125f}};
constexpr ui::Rect kFlashlightIcon{{0.141f, 0.0f}, {0.25f, 0.25f}};

/// The legacy colors of a percentage: red when low, yellow, green, and blue past 100.
glm::vec4 StateColor(int value) {
    if (value <= 20) {
        return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    if (value <= 80) {
        return {1.0f, 1.0f, 0.5f, 1.0f};
    }
    if (value <= 100) {
        return {0.0f, 1.0f, 0.0f, 1.0f};
    }
    return {0.3f, 0.5f, 1.0f, 1.0f};
}

ui::TextStyle Label(glm::vec4 color, ui::Align horizontal) {
    return {.size = kLabelSize,
            .color = color,
            .horizontal = horizontal,
            .vertical = ui::Align::Center,
            .shadowColor = kShadow};
}

} // namespace

core::Result<HudAssets> LoadHud(scene::AssetLibrary& assets) {
    HudAssets hud;
    DAI_TRY_ASSIGN(hud.font, assets.LoadFont("Fonts/Rajdhani-Medium.ttf"));
    DAI_TRY_ASSIGN(hud.boldFont, assets.LoadFont("Fonts/Rajdhani-Bold.ttf"));
    DAI_TRY_ASSIGN(hud.icons, assets.LoadTexture("Factory/hud.png"));
    DAI_TRY_ASSIGN(hud.crosshair, assets.LoadTexture("Factory/crosshair.png"));
    DAI_TRY_ASSIGN(hud.gear, assets.LoadTexture("Factory/gear.png"));
    return hud;
}

void DrawHud(ui::Canvas& canvas, const HudAssets& hud, const Player& player) {
    const glm::vec2 size = canvas.Size();
    if (size.x <= 0.0f) {
        return;
    }

    // Red over everything for a moment after a wound, and for as long as the player is dead.
    if (player.hurtTime > 0.0f || player.dead) {
        const float strength = player.dead ? 0.35f : 0.45f * player.hurtTime / 0.35f;
        canvas.FillRect({{0.0f, 0.0f}, size}, {0.7f, 0.0f, 0.0f, strength});
    }

    const glm::vec2 center = size * 0.5f;
    if (!player.dead) {
        canvas.DrawImage(hud.crosshair, {center - (kCrosshairSize * 0.5f), glm::vec2(kCrosshairSize)},
                         {.color = {1.0f, 1.0f, 1.0f, 0.9f}});
    }

    // Each icon is followed by its value, in a row centered on the icon.
    const float bottomRow = size.y - kMargin - (kIconSize * 0.5f);
    const float upperRow = bottomRow - 55.0f;
    const auto icon = [&](const ui::Rect& source, float left, float row) {
        canvas.DrawImage(hud.icons, {{left, row - (kIconSize * 0.5f)}, glm::vec2(kIconSize)}, {.source = source});
    };
    icon(kHealthIcon, kMargin, upperRow);
    canvas.DrawText(*hud.boldFont, std::format("{}%", std::max(player.health, 0)), {kMargin + 140.0f, upperRow},
                    Label(StateColor(player.health), ui::Align::End));
    icon(kArmorIcon, kMargin, bottomRow);
    canvas.DrawText(*hud.boldFont, std::format("{}%", player.armor), {kMargin + 140.0f, bottomRow},
                    Label(StateColor(player.armor), ui::Align::End));

    const WeaponState& weapon = player.weapons[static_cast<std::size_t>(player.weapon)];
    const float ammoIcon = size.x - kMargin - kIconSize;
    icon(kAmmoIcon, ammoIcon, bottomRow);
    canvas.DrawText(*hud.boldFont, std::format("{} / {}", weapon.magazine, weapon.reserve),
                    {ammoIcon - 12.0f, bottomRow}, Label(glm::vec4(1.0f), ui::Align::End));
    if (player.reloading) {
        canvas.DrawText(*hud.font, "RELOADING", {ammoIcon - 12.0f, upperRow},
                        Label({1.0f, 1.0f, 1.0f, 0.7f}, ui::Align::End));
    }

    if (player.hasFlashlight) {
        const float alpha = player.flashlightOn ? 1.0f : 0.4f;
        canvas.DrawImage(hud.icons, {{size.x - kMargin - kIconSize, 20.0f}, glm::vec2(kIconSize)},
                         {.source = kFlashlightIcon, .color = {1.0f, 1.0f, 1.0f, alpha}});
        // Its battery, red when low (ADR-0058).
        const float battery = std::clamp(player.torch.battery, 0.0f, 1.0f);
        const glm::vec2 bar{size.x - kMargin - kIconSize, 24.0f + kIconSize};
        canvas.FillRect({bar, {kIconSize, 4.0f}}, {0.0f, 0.0f, 0.0f, 0.5f * alpha});
        canvas.FillRect({bar, {kIconSize * battery, 4.0f}}, battery < kLowBattery
                                                                ? glm::vec4(1.0f, 0.2f, 0.1f, alpha)
                                                                : glm::vec4(1.0f, 1.0f, 1.0f, 0.8f * alpha));
    }
    if (player.hasRedCard) {
        canvas.DrawText(*hud.boldFont, "RED CARD", {size.x - kMargin - kIconSize - 16.0f, 20.0f + (kIconSize * 0.5f)},
                        {.size = 28.0f,
                         .color = {1.0f, 0.15f, 0.1f, 1.0f},
                         .horizontal = ui::Align::End,
                         .vertical = ui::Align::Center,
                         .shadowColor = kShadow});
    }

    // The last message fades out over its final second.
    if (player.messageTime > 0.0f && !player.message.empty()) {
        const float alpha = std::min(player.messageTime, 1.0f);
        canvas.DrawText(*hud.font, player.message, {center.x, center.y - 120.0f},
                        {.size = kMessageSize,
                         .color = {1.0f, 0.85f, 0.4f, alpha},
                         .horizontal = ui::Align::Center,
                         .vertical = ui::Align::Center,
                         .shadowColor = {0.0f, 0.0f, 0.0f, 0.6f * alpha}});
    }
}

void DrawLoading(ui::Canvas& canvas, const HudAssets& hud, bool coverScreen, float seconds) {
    const glm::vec2 size = canvas.Size();
    canvas.FillRect({{0.0f, 0.0f}, size}, {0.0f, 0.0f, 0.0f, coverScreen ? 1.0f : 0.6f});
    canvas.DrawText(*hud.boldFont, "LOADING...", size * 0.5f,
                    {.size = 80.0f,
                     .color = glm::vec4(1.0f),
                     .horizontal = ui::Align::Center,
                     .vertical = ui::Align::Center,
                     .shadowColor = kShadow});
    // As in the legacy CLoadingView: 256 units in the corner, a quarter turn per second.
    constexpr float kGearSize = 256.0f;
    constexpr float kTurnsPerSecond = 0.25f;
    canvas.DrawImage(hud.gear, {size - kGearSize, glm::vec2(kGearSize)},
                     {.rotation = glm::two_pi<float>() * kTurnsPerSecond * seconds});
}

} // namespace sandbox
