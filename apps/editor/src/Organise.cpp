// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Organise.hpp"

#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace editor {

using daidalos::scene::Level;
using daidalos::scene::LevelEntity;

namespace {

/// Whether a test holds for an entity or any entity above it.
template <class Test>
bool UpTheChain(const Level& level, std::uint64_t id, Test test) {
    // A cycle in a hand-edited file must not hang the editor.
    for (std::size_t steps = 0; id != 0 && steps <= level.entities.size(); ++steps) {
        const LevelEntity* entity = level.Find(id);
        if (entity == nullptr) {
            return false;
        }
        if (test(*entity)) {
            return true;
        }
        id = entity->parent;
    }
    return false;
}

} // namespace

bool HiddenInEditor(const Level& level, std::uint64_t id) {
    return UpTheChain(level, id, [](const LevelEntity& entity) { return entity.editing.hidden; });
}

bool LockedInEditor(const Level& level, std::uint64_t id) {
    return UpTheChain(level, id, [](const LevelEntity& entity) { return entity.editing.locked; });
}

Level ShownLevel(const Level& level, const std::set<std::uint64_t>& isolated) {
    Level shown;
    shown.environment = level.environment;
    std::set<std::uint64_t> kept;
    if (!isolated.empty()) {
        // The isolated, what is under them, and what holds them, without which they could not stand.
        for (const LevelEntity& entity : level.entities) {
            if (UpTheChain(level, entity.id, [&](const LevelEntity& above) { return isolated.contains(above.id); })) {
                kept.insert(entity.id);
                for (std::uint64_t parent = entity.parent; parent != 0 && kept.insert(parent).second;) {
                    const LevelEntity* holder = level.Find(parent);
                    parent = holder != nullptr ? holder->parent : 0;
                }
            }
        }
    }
    for (const LevelEntity& entity : level.entities) {
        if ((isolated.empty() || kept.contains(entity.id)) && !HiddenInEditor(level, entity.id)) {
            shown.entities.push_back(entity);
        }
    }
    return shown;
}

std::string TidyFolder(std::string_view folder) {
    std::string tidy;
    while (!folder.empty()) {
        const std::size_t slash = folder.find_first_of("/\\");
        std::string_view part = folder.substr(0, slash);
        folder = slash == std::string_view::npos ? std::string_view() : folder.substr(slash + 1);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (part.empty()) {
            continue;
        }
        if (!tidy.empty()) {
            tidy += '/';
        }
        tidy += part;
    }
    return tidy;
}

std::vector<std::string> FoldersOf(const Level& level) {
    std::set<std::string> folders;
    for (const LevelEntity& entity : level.entities) {
        for (std::string folder = entity.editing.folder; !folder.empty(); folder = ParentFolder(folder)) {
            folders.insert(folder);
        }
    }
    return {folders.begin(), folders.end()};
}

std::string ParentFolder(std::string_view folder) {
    const std::size_t slash = folder.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(folder.substr(0, slash));
}

bool InFolder(std::string_view entityFolder, std::string_view folder) {
    return entityFolder == folder || (entityFolder.size() > folder.size() && entityFolder.starts_with(folder) &&
                                      entityFolder[folder.size()] == '/');
}

std::vector<glm::vec3> Aligned(std::span<const glm::vec3> positions, int axis, AlignTo to) {
    std::vector<glm::vec3> aligned(positions.begin(), positions.end());
    if (positions.empty()) {
        return aligned;
    }
    float low = positions.front()[axis];
    float high = low;
    for (const glm::vec3& position : positions) {
        low = std::min(low, position[axis]);
        high = std::max(high, position[axis]);
    }
    const float target = to == AlignTo::Min ? low : to == AlignTo::Max ? high : (low + high) * 0.5f;
    for (glm::vec3& position : aligned) {
        position[axis] = target;
    }
    return aligned;
}

std::vector<glm::vec3> Spread(std::span<const glm::vec3> positions, int axis) {
    std::vector<glm::vec3> spread(positions.begin(), positions.end());
    if (positions.size() < 3) {
        return spread;
    }
    std::vector<std::size_t> order(positions.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order,
                             [&](std::size_t a, std::size_t b) { return positions[a][axis] < positions[b][axis]; });
    const float low = positions[order.front()][axis];
    const float high = positions[order.back()][axis];
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        spread[order[rank]][axis] =
            low + (high - low) * static_cast<float>(rank) / static_cast<float>(order.size() - 1);
    }
    return spread;
}

glm::vec3 PivotPoint(Pivot pivot, const glm::vec3& origin, const Bounds& bounds) {
    if (pivot == Pivot::Origin || bounds.Empty()) {
        return origin;
    }
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    return pivot == Pivot::Center ? center : glm::vec3(center.x, bounds.min.y, center.z);
}

} // namespace editor
