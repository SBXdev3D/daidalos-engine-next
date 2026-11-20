// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/scene/Level.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace editor {

/// A file that names an asset (ADR-0117): its path under the asset root, and where in it.
struct AssetReference {
    std::string file;
    std::string where;
};

/// Every level, prefab, effect, ribbon and sequence under the root that names an asset, a path under the root (a
/// sky by its faces' prefix). Texture sources (`Source` folders) are left out.
[[nodiscard]] std::vector<AssetReference> FindReferences(const std::filesystem::path& root, const std::string& asset);

/// Points a level's paths that name one asset at another, both under the root; the level is read from levelPath.
/// Returns how many paths changed.
std::size_t RepointLevel(daidalos::scene::Level& level, const std::string& levelPath, const std::string& from,
                         const std::string& to);

/// Moves or renames an asset under the root, with its import settings, and rewrites every file that names it,
/// except the level at skip, which the editor holds open and repoints itself. A model whose buffers or images lie
/// beside it only changes its name in its folder. Returns the files rewritten.
[[nodiscard]] daidalos::core::Result<std::vector<std::string>> MoveAsset(const std::filesystem::path& root,
                                                                         const std::string& from, const std::string& to,
                                                                         const std::string& skip = {});

} // namespace editor
