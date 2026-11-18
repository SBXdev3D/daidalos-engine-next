// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <span>
#include <vector>

// Portal visibility (ADR-0089): rooms as boxes, the openings between them where two boxes overlap, and the
// rooms the camera sees through those openings.
namespace daidalos::renderer {

/// A room, or a doorway between two rooms: a box in the world, aligned with its axes.
struct VisibilityCell {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// The cells seen from a camera. Cells holding the camera are seen. From a seen cell, a cell whose box overlaps
/// its box (the portal: their intersection) is seen if the portal's corners project inside the part of the
/// screen the way there left, which narrows to the portal's on-screen rectangle at each step. A portal with a
/// corner behind the camera leaves the whole screen. Returns one flag per cell, all set when the camera is in
/// no cell: then nothing is known, and everything may be seen.
[[nodiscard]] std::vector<bool> VisibleCells(std::span<const VisibilityCell> cells, const glm::mat4& viewProjection,
                                             const glm::vec3& camera);

/// Whether a sphere may be seen: it touches a seen cell, or no cell at all (what lies outside the cells is
/// always drawn).
[[nodiscard]] bool MayBeSeen(std::span<const VisibilityCell> cells, const std::vector<bool>& visible,
                             const glm::vec3& center, float radius);

} // namespace daidalos::renderer
