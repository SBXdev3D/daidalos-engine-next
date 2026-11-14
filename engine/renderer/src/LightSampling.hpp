// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/renderer/SceneView.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace daidalos::renderer {

/// One entry of an alias table (Walker 1977, Vose 1991), which draws an index with a probability given by
/// its weight from two uniform numbers in constant time: the first picks an entry, which is kept with the
/// probability `keep`, its alias otherwise. Matches AliasEntry in shaders/Lights.slangh.
struct AliasEntry {
    float keep = 1.0f;
    std::uint32_t alias = 0;
    /// The probability of drawing this entry, which samples are weighed by.
    float probability = 0.0f;
    std::uint32_t padding = 0;
};
static_assert(sizeof(AliasEntry) == 16);

/// The alias table of these weights. Negative and non-finite weights count as zero; when every weight is
/// zero, every entry is as likely. Empty for no weights.
[[nodiscard]] std::vector<AliasEntry> BuildAliasTable(std::span<const float> weights);

/// The index an alias table draws for two numbers in [0, 1), as shaders draw it.
[[nodiscard]] std::uint32_t SampleAliasTable(std::span<const AliasEntry> table, float pick, float keep);

/// What lights are sampled by: the luminous flux of a light, in lumens (ADR-0044), from its candela and
/// cone. A directional light has no finite flux: it counts as what it sends through a disk of
/// kDirectionalReach meters, which is only a guess, since sampling stays unbiased whatever the powers.
[[nodiscard]] float LightPower(const Light& light);
inline constexpr float kDirectionalReach = 10.0f;

/// The luminance of a linear color.
[[nodiscard]] float Luminance(const glm::vec3& color);

/// What names a light from one frame to the next: its id, or its position in the frame without one.
[[nodiscard]] std::uint64_t IdentityKey(SceneId id, std::uint32_t position);

/// The lights of one source of a frame, consecutive in its list: an analytic light, or the triangles of an
/// emissive draw, and what names the source from frame to frame.
struct LightRange {
    std::uint64_t key = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

/// Where each light of the previous frame is in this frame's list, or kGone (ADR-0044): a source found
/// again by its key keeps its lights if it has as many. lightCount is the previous frame's number of lights.
inline constexpr std::uint32_t kGone = 0xFFFFFFFF;
[[nodiscard]] std::vector<std::uint32_t> MapLights(std::span<const LightRange> previous,
                                                   std::span<const LightRange> current, std::uint32_t lightCount);

} // namespace daidalos::renderer
