// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Quality.hpp>

#include <array>
#include <cstddef>

namespace daidalos::app {

namespace {

// The presets (ADR-0052), from what each setting cost in the factory at 1080p on an RTX 3090, where High
// took 7.7 ms: the render scale is most of the cost (3.9 ms at 67%), then a second ray of indirect light
// (+2.3 ms), reflections on rough surfaces (+1.1 ms up to roughness 0.9), ReSTIR's spatial reuse (1.0 ms)
// and the surface lit before the radiance cache is read (0.7 ms). The fog costs 0.4 ms in all.
constexpr std::array<QualitySettings, 4> kPresets{{
    // Low: 58% of the resolution at most, light from the cache alone, sharp reflections only, so every pixel
    // traces its own: no rough ones to halve.
    {.renderScale = 0.58,
     .minRenderScale = 0.5,
     .giBounces = 0,
     .giPixelsPerRay = 2,
     .reflectionRoughness = 0.3,
     .reflectionPixelsPerRay = 1,
     .restirSpatial = false,
     .restirCandidates = 4,
     .fogSlices = 32,
     .fogTile = 16,
     .particleDensity = 0.5,
     .tracedParticles = false},
    // Medium, the minimum tier's (an RTX 3060 at 1080p): two thirds of the resolution at most, and half the
    // pixels trace indirect light, and pixels draw four lights before they keep one (ADR-0093).
    {.renderScale = 0.67,
     .minRenderScale = 0.5,
     .giPixelsPerRay = 2,
     .reflectionRoughness = 0.45,
     .restirCandidates = 4,
     .fogSlices = 48},
    // High: what the engine is tuned for. Medium, High and Ultra trace half the rough reflections, in turns
    // (ADR-0096): 0.1 to 0.35 ms less on the loading floor, the same image.
    {},
    // Ultra: two surfaces lit before the cache, reflections on rough surfaces, finer fog, never below two
    // thirds of the resolution. A second ray of indirect light doubled its cost (+4.2 to 5.2 ms) for noise the
    // denoiser already removes, and was dropped (ADR-0090).
    {.minRenderScale = 0.67, .giBounces = 2, .reflectionRoughness = 0.9, .fogSlices = 128, .fogTile = 8},
}};

constexpr std::array<std::string_view, 4> kNames{"low", "medium", "high", "ultra"};

// A frame's GPU time at each preset over High's at full scale, measured in the dressed factory with its
// visibility cells (ADR-0090), in the start room and the loading floor at 1080p on an RTX 3090: Low 3.7 and
// 4.7 ms, Medium 6.4 and 7.6, High 11.7 and 15.0, Ultra 14.6 and 16.9. Since Low and Medium trace half the
// pixels' indirect light and draw four ReSTIR candidates (ADR-0093): Low 3.5 and 4.3 ms, Medium 5.6 and 6.7,
// where High took 13.1 and 15.6 in the same run.
constexpr std::array<double, 4> kShareOfHigh{0.28, 0.43, 1.0, 1.2};
// A preset must fit the frame with this margin: the factory's busiest views cost more than the start.
constexpr double kMargin = 1.25;

} // namespace

QualitySettings SettingsOf(Quality quality) {
    return kPresets[static_cast<std::size_t>(quality)];
}

std::string_view NameOf(Quality quality) {
    return kNames[static_cast<std::size_t>(quality)];
}

std::optional<Quality> QualityNamed(std::string_view name) {
    for (std::size_t index = 0; index < kNames.size(); ++index) {
        if (kNames[index] == name) {
            return kQualities[index];
        }
    }
    return std::nullopt;
}

Quality ChooseQuality(double highMilliseconds, double targetFps) {
    const double budget = 1000.0 / targetFps;
    for (std::size_t index = kQualities.size(); index > 1; --index) {
        if (highMilliseconds * kShareOfHigh[index - 1] * kMargin <= budget) {
            return kQualities[index - 1];
        }
    }
    return Quality::Low;
}

} // namespace daidalos::app
