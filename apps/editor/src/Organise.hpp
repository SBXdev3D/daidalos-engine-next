// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Picking.hpp"

namespace editor {

/// The level as the editor's view shows it (ADR-0107): without hidden entities and what is under them, and, while
/// some are isolated, with only those, what is under them and what holds them.
[[nodiscard]] daidalos::scene::Level ShownLevel(const daidalos::scene::Level& level,
                                                const std::set<std::uint64_t>& isolated);

/// Whether an entity, or one holding it, is hidden or locked in the editor.
[[nodiscard]] bool HiddenInEditor(const daidalos::scene::Level& level, std::uint64_t id);
[[nodiscard]] bool LockedInEditor(const daidalos::scene::Level& level, std::uint64_t id);

/// A folder path as typed, tidied: no empty parts, no spaces around them, '/' between them.
[[nodiscard]] std::string TidyFolder(std::string_view folder);
/// The folders of the level's entities and every folder above them, sorted, so that a parent comes before its
/// children.
[[nodiscard]] std::vector<std::string> FoldersOf(const daidalos::scene::Level& level);
/// The folder holding another, empty at the top.
[[nodiscard]] std::string ParentFolder(std::string_view folder);
/// Whether an entity's folder is the given one or one under it.
[[nodiscard]] bool InFolder(std::string_view entityFolder, std::string_view folder);

/// Where a selection's positions go when lined up on one axis: their lowest, middle or highest (ADR-0107).
enum class AlignTo { Min, Center, Max };
[[nodiscard]] std::vector<glm::vec3> Aligned(std::span<const glm::vec3> positions, int axis, AlignTo to);
/// The positions spread evenly on one axis between the two farthest, keeping their order along it.
[[nodiscard]] std::vector<glm::vec3> Spread(std::span<const glm::vec3> positions, int axis);

/// Where the gizmo turns and scales a selection around (ADR-0107).
enum class Pivot { Origin, Center, Bottom };
/// The pivot of a selection whose primary entity is at origin and whose content fills bounds.
[[nodiscard]] glm::vec3 PivotPoint(Pivot pivot, const glm::vec3& origin, const Bounds& bounds);

} // namespace editor
