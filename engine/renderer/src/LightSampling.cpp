// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "LightSampling.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace daidalos::renderer {

std::vector<AliasEntry> BuildAliasTable(std::span<const float> weights) {
    const std::size_t count = weights.size();
    std::vector<AliasEntry> table(count);
    if (count == 0) {
        return table;
    }
    double total = 0.0;
    for (const float weight : weights) {
        total += std::isfinite(weight) && weight > 0.0f ? weight : 0.0;
    }
    // Each entry's weight, scaled so that their average is 1: an entry above 1 lends to one below.
    std::vector<double> scaled(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float weight = weights[index];
        const double probability =
            total > 0.0 ? (std::isfinite(weight) && weight > 0.0f ? weight / total : 0.0) : 1.0 / static_cast<double>(count);
        table[index].probability = static_cast<float>(probability);
        scaled[index] = probability * static_cast<double>(count);
    }
    // Vose's method: each small entry is topped up by a large one, which becomes its alias.
    std::vector<std::uint32_t> small;
    std::vector<std::uint32_t> large;
    for (std::uint32_t index = 0; index < count; ++index) {
        (scaled[index] < 1.0 ? small : large).push_back(index);
    }
    while (!small.empty() && !large.empty()) {
        const std::uint32_t less = small.back();
        small.pop_back();
        const std::uint32_t more = large.back();
        table[less].keep = static_cast<float>(scaled[less]);
        table[less].alias = more;
        scaled[more] -= 1.0 - scaled[less];
        if (scaled[more] < 1.0) {
            large.pop_back();
            small.push_back(more);
        }
    }
    // What is left is 1, up to rounding: kept for certain.
    for (const std::uint32_t index : small) {
        table[index].keep = 1.0f;
        table[index].alias = index;
    }
    for (const std::uint32_t index : large) {
        table[index].keep = 1.0f;
        table[index].alias = index;
    }
    return table;
}

std::uint32_t SampleAliasTable(std::span<const AliasEntry> table, float pick, float keep) {
    const auto count = static_cast<std::uint32_t>(table.size());
    const std::uint32_t index = std::min(static_cast<std::uint32_t>(pick * static_cast<float>(count)), count - 1);
    return keep < table[index].keep ? index : table[index].alias;
}

float Luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float LightPower(const Light& light) {
    const float luminance = std::max(Luminance(light.color), 0.0f);
    constexpr float kPi = std::numbers::pi_v<float>;
    switch (light.type) {
    case LightType::Point:
        return 4.0f * kPi * luminance;
    case LightType::Spot: {
        // The solid angle of a cone halfway between the inner and the outer angle.
        const float angle = std::clamp((light.innerAngle + light.outerAngle) * 0.5f, 0.0f, kPi);
        return 2.0f * kPi * (1.0f - std::cos(angle)) * luminance;
    }
    case LightType::Directional:
        return kPi * kDirectionalReach * kDirectionalReach * luminance;
    }
    return 0.0f;
}

std::uint64_t IdentityKey(SceneId id, std::uint32_t position) {
    // Ids name entities by their index and generation, whose top bit is never set.
    constexpr std::uint64_t kPositional = std::uint64_t{1} << 63U;
    return id != 0 ? id : (kPositional | position);
}

std::vector<std::uint32_t> MapIdentities(std::span<const std::uint64_t> previous,
                                         std::span<const std::uint64_t> current) {
    std::unordered_map<std::uint64_t, std::uint32_t> where;
    where.reserve(current.size());
    for (std::uint32_t index = 0; index < current.size(); ++index) {
        where.emplace(current[index], index);
    }
    std::vector<std::uint32_t> mapped(previous.size(), kGone);
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (const auto found = where.find(previous[index]); found != where.end()) {
            mapped[index] = found->second;
        }
    }
    return mapped;
}

} // namespace daidalos::renderer
