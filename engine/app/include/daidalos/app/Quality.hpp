// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace daidalos::app {

/// Quality presets (ADR-0052): each sets the cost of every rendering feature that has one. High is the
/// look the engine is tuned for; the others trade it for frame time.
enum class Quality : std::uint8_t { Low, Medium, High, Ultra };

inline constexpr std::array<Quality, 4> kQualities{Quality::Low, Quality::Medium, Quality::High, Quality::Ultra};

/// The settings a preset decides, by the renderer.* variable each stands for. A variable that a config
/// file, the command line or the player sets wins over its preset.
struct QualitySettings {
    /// renderer.render_scale and renderer.min_render_scale: what dynamic resolution moves between.
    double renderScale = 1.0;
    double minRenderScale = 0.5;
    /// renderer.indirect_light, renderer.gi_bounces, renderer.gi_rays, renderer.gi_new_rays and
    /// renderer.gi_pixels_per_ray.
    bool indirectLight = true;
    std::int64_t giBounces = 1;
    std::int64_t giRays = 1;
    std::int64_t giNewRays = 1;
    std::int64_t giPixelsPerRay = 1;
    /// renderer.reflections, renderer.reflection_roughness and renderer.reflection_pixels_per_ray.
    bool reflections = true;
    double reflectionRoughness = 0.6;
    std::int64_t reflectionPixelsPerRay = 2;
    /// renderer.restir_spatial and renderer.restir_candidates.
    bool restirSpatial = true;
    std::int64_t restirCandidates = 8;
    /// renderer.volumetric_fog, renderer.fog_slices and renderer.fog_tile.
    bool volumetricFog = true;
    std::int64_t fogSlices = 64;
    std::int64_t fogTile = 16;
    /// renderer.particle_density: multiplies what emitters and weather emit (ADR-0053).
    double particleDensity = 1.0;
    /// renderer.traced_particles: particles in the frame's acceleration structure, their shadows and
    /// reflections (ADR-0062).
    bool tracedParticles = true;

    bool operator==(const QualitySettings&) const = default;
};

[[nodiscard]] QualitySettings SettingsOf(Quality quality);

/// "low", "medium", "high" or "ultra", as renderer.quality holds it.
[[nodiscard]] std::string_view NameOf(Quality quality);
[[nodiscard]] std::optional<Quality> QualityNamed(std::string_view name);

/// The best preset that holds targetFps on this GPU, from the GPU time of a frame measured at High at
/// full render scale: each preset's frame is estimated from its measured share of High's, and must fit
/// the frame's budget with a margin.
[[nodiscard]] Quality ChooseQuality(double highMilliseconds, double targetFps);

} // namespace daidalos::app
