// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

/// What a file is to the asset browser.
enum class AssetKind { Folder, Model, Sky, Effect, Ribbon, Level, Prefab, Decal, Sequence, Sound, Other };

/// An asset of a folder, as the browser lists it.
struct AssetEntry {
    AssetKind kind = AssetKind::Other;
    /// What the browser shows: the file's name, a sky's prefix.
    std::string name;
    /// Under the asset root, with '/'. A sky's is its faces' prefix ("Factory/Sky/sky1"), as levels name it.
    std::string path;
};

/// The kind of a file, by its name. A sky is its "_front.png" face; a decal is an image in a folder named
/// "Decals", other than a normal map ("_normal.png").
[[nodiscard]] AssetKind KindOf(const std::filesystem::path& file);

/// For a face of a sky, "sky1" of "sky1_right.png" and the other five faces; otherwise nothing.
[[nodiscard]] std::optional<std::string> SkyPrefix(std::string_view fileName);

/// The folders, then the assets, of a folder under the asset root, each sorted by name. A sky shows once, when
/// its six faces are there; other images and files are left out.
[[nodiscard]] std::vector<AssetEntry> ListFolder(const std::filesystem::path& assetRoot, std::string_view folder);

/// Copies a glTF model into a directory, with the buffers and images it reads, keeping their places relative to
/// it. A model that reads files beside it goes in a folder of its own name. Refuses a model whose files are
/// outside its directory, and one that would replace a file. Returns the copied model's path.
[[nodiscard]] daidalos::core::Result<std::filesystem::path> ImportModel(const std::filesystem::path& source,
                                                                        const std::filesystem::path& directory);

/// Copies the six faces of the sky that face is one of into a directory. Returns the sky's prefix there
/// (directory/prefix), as a level's environment names it.
[[nodiscard]] daidalos::core::Result<std::filesystem::path> ImportSky(const std::filesystem::path& face,
                                                                      const std::filesystem::path& directory);

} // namespace editor
