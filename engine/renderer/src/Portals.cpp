// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Portals.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>

namespace daidalos::renderer {

namespace {

/// The deepest a walk through portals goes.
constexpr std::size_t kMaxDepth = 24;

/// A part of the screen in normalized device coordinates.
struct Rect {
    glm::vec2 min{-1.0f};
    glm::vec2 max{1.0f};
    [[nodiscard]] bool Empty() const { return min.x >= max.x || min.y >= max.y; }
};

/// Boxes that only touch make no portal: two rooms drawn up to the same wall do not see each other through it.
/// An opening is a cell, a doorway, that overlaps both.
bool Overlap(const VisibilityCell& a, const VisibilityCell& b, VisibilityCell& portal) {
    portal.min = glm::max(a.min, b.min);
    portal.max = glm::min(a.max, b.max);
    return glm::all(glm::lessThan(portal.min, portal.max));
}

bool Contains(const VisibilityCell& cell, const glm::vec3& point) {
    return glm::all(glm::greaterThanEqual(point, cell.min)) && glm::all(glm::lessThanEqual(point, cell.max));
}

/// Where a box is on screen: nothing when it is wholly behind the camera, the whole screen when only some of it
/// is (the camera is in or at the opening).
Rect Project(const VisibilityCell& box, const glm::mat4& viewProjection) {
    Rect rect{glm::vec2(1e9f), glm::vec2(-1e9f)};
    int behind = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) != 0 ? box.max.x : box.min.x, (corner & 2) != 0 ? box.max.y : box.min.y,
                              (corner & 4) != 0 ? box.max.z : box.min.z);
        const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
        if (clip.w <= 1e-3f) {
            ++behind;
            continue;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        rect.min = glm::min(rect.min, ndc);
        rect.max = glm::max(rect.max, ndc);
    }
    if (behind == 8) {
        return Rect{glm::vec2(1.0f), glm::vec2(-1.0f)};
    }
    return behind > 0 ? Rect{} : rect;
}

struct Walk {
    std::span<const VisibilityCell> cells;
    const glm::mat4* viewProjection = nullptr;
    std::vector<std::vector<std::size_t>> neighbours;
    std::vector<std::vector<VisibilityCell>> portals;
    std::vector<bool> visible;
    std::vector<bool> onPath;

    void Visit(std::size_t cell, const Rect& through, std::size_t depth) {
        visible[cell] = true;
        if (depth >= kMaxDepth) {
            return;
        }
        onPath[cell] = true;
        for (std::size_t index = 0; index < neighbours[cell].size(); ++index) {
            const std::size_t next = neighbours[cell][index];
            if (onPath[next]) {
                continue;
            }
            const Rect seen = Project(portals[cell][index], *viewProjection);
            const Rect narrowed{glm::max(through.min, seen.min), glm::min(through.max, seen.max)};
            if (!narrowed.Empty()) {
                Visit(next, narrowed, depth + 1);
            }
        }
        onPath[cell] = false;
    }
};

} // namespace

std::vector<bool> VisibleCells(std::span<const VisibilityCell> cells, const glm::mat4& viewProjection,
                               const glm::vec3& camera) {
    Walk walk{.cells = cells, .viewProjection = &viewProjection};
    walk.visible.assign(cells.size(), false);
    walk.onPath.assign(cells.size(), false);
    walk.neighbours.resize(cells.size());
    walk.portals.resize(cells.size());
    for (std::size_t a = 0; a < cells.size(); ++a) {
        for (std::size_t b = 0; b < cells.size(); ++b) {
            VisibilityCell portal;
            if (a != b && Overlap(cells[a], cells[b], portal)) {
                walk.neighbours[a].push_back(b);
                walk.portals[a].push_back(portal);
            }
        }
    }
    bool inside = false;
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        if (Contains(cells[cell], camera)) {
            inside = true;
            walk.Visit(cell, Rect{}, 0);
        }
    }
    if (!inside) {
        walk.visible.assign(cells.size(), true);
    }
    return walk.visible;
}

bool MayBeSeen(std::span<const VisibilityCell> cells, const std::vector<bool>& visible, const glm::vec3& center,
               float radius) {
    bool inAny = false;
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        // The distance from the sphere's center to the box.
        const glm::vec3 nearest = glm::clamp(center, cells[cell].min, cells[cell].max);
        if (glm::distance(nearest, center) > radius) {
            continue;
        }
        if (visible[cell]) {
            return true;
        }
        inAny = true;
    }
    return !inAny;
}

} // namespace daidalos::renderer
