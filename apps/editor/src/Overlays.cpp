// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Overlays.hpp"

#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

Category CategoryOf(const daidalos::scene::LevelEntity& entity) {
    if (entity.light) {
        return Category::Lights;
    }
    if (entity.particles || entity.ribbon || entity.lightning || entity.weather || entity.attractor) {
        return Category::Effects;
    }
    if (entity.trigger) {
        return Category::Triggers;
    }
    if (entity.cell) {
        return Category::Rooms;
    }
    if (entity.decal) {
        return Category::Decals;
    }
    if (entity.fog || entity.wind || entity.wet) {
        return Category::Volumes;
    }
    if (entity.game) {
        return Category::Game;
    }
    if (!entity.model.empty() || !entity.prefab.empty() || entity.block) {
        return Category::Models;
    }
    return Category::Other;
}

std::string_view CategoryName(Category category) {
    constexpr std::array<std::string_view, kCategoryCount> kNames{
        "Lights", "Effects", "Fog, wind and water", "Triggers",      "Rooms and doorways",
        "Decals", "Game",    "Models and prefabs",  "Empty entities"};
    return kNames[static_cast<std::size_t>(category)];
}

std::vector<std::string> ApplyShowFlags(ShowFlags& show, std::string_view names) {
    std::vector<std::string> unknown;
    while (!names.empty()) {
        const std::size_t comma = names.find(',');
        std::string_view name = names.substr(0, comma);
        names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
        while (!name.empty() && name.front() == ' ') {
            name.remove_prefix(1);
        }
        while (!name.empty() && name.back() == ' ') {
            name.remove_suffix(1);
        }
        if (name.empty()) {
            continue;
        }
        if (name == "light_reach") {
            show.lightReach = true;
        } else if (name == "collision") {
            show.collision = true;
        } else if (name == "navigation") {
            show.navigation = true;
        } else if (name == "portals") {
            show.portals = true;
        } else if (name == "game_view") {
            show.gameView = true;
        } else if (name == "no_grid") {
            show.grid = false;
        } else if (name == "no_decals") {
            show.decals = false;
        } else if (name == "no_particles") {
            show.particles = false;
        } else if (name == "no_fog") {
            show.fog = false;
        } else {
            unknown.emplace_back(name);
        }
    }
    return unknown;
}

float DepthTest::Visibility(const glm::vec3& point) const {
    if (m_picture == nullptr || m_picture->width == 0 || m_picture->height == 0 || m_picture->meters.empty()) {
        return 1.0f;
    }
    const glm::vec4 clip = m_picture->viewProjection * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0f) {
        return 1.0f;
    }
    const float u = clip.x / clip.w * 0.5f + 0.5f;
    const float v = 0.5f - clip.y / clip.w * 0.5f;
    if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) {
        return 1.0f;
    }
    const auto x = std::min(static_cast<std::uint32_t>(u * static_cast<float>(m_picture->width)), m_picture->width - 1);
    const auto y =
        std::min(static_cast<std::uint32_t>(v * static_cast<float>(m_picture->height)), m_picture->height - 1);
    const float scene = m_picture->meters[std::size_t{y} * m_picture->width + x];
    // A little slack, growing with distance, so that what lies on a surface stays in front of it.
    return clip.w > scene * 1.02f + 0.05f ? kHidden : 1.0f;
}

std::vector<Bounds> Openings(std::span<const Bounds> rooms) {
    std::vector<Bounds> openings;
    for (std::size_t first = 0; first < rooms.size(); ++first) {
        for (std::size_t second = first + 1; second < rooms.size(); ++second) {
            const glm::vec3 low = glm::max(rooms[first].min, rooms[second].min);
            const glm::vec3 high = glm::min(rooms[first].max, rooms[second].max);
            if (low.x < high.x && low.y < high.y && low.z < high.z) {
                Bounds opening;
                opening.Add(low);
                opening.Add(high);
                openings.push_back(opening);
            }
        }
    }
    return openings;
}

Bounds UnitBoxBounds(const glm::mat4& matrix) {
    Bounds unit;
    unit.Add(glm::vec3(-0.5f));
    unit.Add(glm::vec3(0.5f));
    return unit.Transformed(matrix);
}

} // namespace editor
