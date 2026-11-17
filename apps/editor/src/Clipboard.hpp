// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace editor {

/// The entities to copy for a selection: each selected entity with everything under it, once, parents before
/// their children, in the level's order. A selected entity under another selected one comes with its parent.
[[nodiscard]] std::vector<daidalos::scene::LevelEntity> CopyEntities(const daidalos::scene::Level& level,
                                                                     std::span<const std::uint64_t> selection);

/// Of copied entities, the ones whose parent was not copied with them: what was selected.
[[nodiscard]] std::vector<std::uint64_t> CopiedRoots(std::span<const daidalos::scene::LevelEntity> copies);

/// Adds copies to a level under new ids. References between the copies (parents, a ribbon's target, a
/// lightning's light, the model an emitter burns on) follow them to the new ids; references to entities that
/// were not copied stay when the level has them, and are cleared otherwise, so the level stays valid. A copy
/// whose parent was not copied keeps it when the level has it, and goes to the root otherwise. Returns the new
/// ids of the copies' roots.
std::vector<std::uint64_t> PasteEntities(daidalos::scene::Level& level,
                                         std::vector<daidalos::scene::LevelEntity> copies);

/// Whether an entity is the ancestor of another, or the same, in a level: a parent it cannot be moved under.
[[nodiscard]] bool IsAncestor(const daidalos::scene::Level& level, std::uint64_t ancestor, std::uint64_t entity);

} // namespace editor
