// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Import.hpp"

#include <daidalos/resources/ModelData.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <set>

using namespace daidalos;

namespace editor {

namespace {

constexpr std::string_view kModule = "editor";
// The order of a cube's faces, as levels name them (ADR-0036).
constexpr std::array<std::string_view, 6> kFaces{"_right", "_left", "_up", "_down", "_front", "_back"};
constexpr std::string_view kSkyExtension = ".png";

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

core::Result<void> Copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    if (std::filesystem::exists(to, error)) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("{} exists already: nothing was imported", to.generic_string()));
    }
    std::filesystem::create_directories(to.parent_path(), error);
    if (!std::filesystem::copy_file(from, to, error)) {
        return core::MakeError(
            core::ErrorCode::IoError, kModule,
            std::format("Cannot copy {} to {}: {}", from.generic_string(), to.generic_string(), error.message()));
    }
    return {};
}

} // namespace

AssetKind KindOf(const std::filesystem::path& file) {
    const std::string name = Lower(file.filename().generic_string());
    if (name.ends_with(".gltf") || name.ends_with(".glb")) {
        return AssetKind::Model;
    }
    if (name.ends_with(".effect.json")) {
        return AssetKind::Effect;
    }
    if (name.ends_with(".ribbon.json")) {
        return AssetKind::Ribbon;
    }
    if (name.ends_with(".sequence.json")) {
        return AssetKind::Sequence;
    }
    if (name.ends_with(".prefab.json")) {
        return AssetKind::Prefab;
    }
    if (name.ends_with(".level.json") && !name.ends_with(".play.level.json")) {
        return AssetKind::Level;
    }
    if (name.ends_with("_front.png")) {
        return AssetKind::Sky;
    }
    if ((name.ends_with(".png") || name.ends_with(".ktx2")) && !name.ends_with("_normal.png") &&
        Lower(file.parent_path().filename().generic_string()) == "decals") {
        return AssetKind::Decal;
    }
    return AssetKind::Other;
}

std::optional<std::string> SkyPrefix(std::string_view fileName) {
    const std::string name(fileName);
    if (!Lower(name).ends_with(kSkyExtension)) {
        return std::nullopt;
    }
    const std::string stem = name.substr(0, name.size() - kSkyExtension.size());
    for (const std::string_view face : kFaces) {
        if (stem.size() > face.size() && stem.ends_with(face)) {
            return stem.substr(0, stem.size() - face.size());
        }
    }
    return std::nullopt;
}

std::vector<AssetEntry> ListFolder(const std::filesystem::path& assetRoot, std::string_view folder) {
    std::vector<AssetEntry> folders;
    std::vector<AssetEntry> assets;
    const std::filesystem::path directory = assetRoot / std::filesystem::path(folder);
    const auto under = [&folder](const std::string& name) {
        return folder.empty() ? name : std::string(folder) + "/" + name;
    };
    std::error_code error;
    std::set<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        files.insert(entry.path().filename().generic_string());
    }
    for (const std::string& name : files) {
        if (std::filesystem::is_directory(directory / name, error)) {
            folders.push_back({.kind = AssetKind::Folder, .name = name, .path = under(name)});
            continue;
        }
        const AssetKind kind = KindOf(std::filesystem::path(folder) / name);
        if (kind == AssetKind::Other) {
            continue;
        }
        if (kind == AssetKind::Sky) {
            const std::string prefix = *SkyPrefix(name);
            const bool complete = std::ranges::all_of(kFaces, [&](std::string_view face) {
                return files.contains(prefix + std::string(face) + std::string(kSkyExtension));
            });
            if (complete) {
                assets.push_back({.kind = kind, .name = prefix, .path = under(prefix)});
            }
            continue;
        }
        assets.push_back({.kind = kind, .name = name, .path = under(name)});
    }
    folders.insert(folders.end(), assets.begin(), assets.end());
    return folders;
}

core::Result<std::filesystem::path> ImportModel(const std::filesystem::path& source,
                                                const std::filesystem::path& directory) {
    if (KindOf(source) != AssetKind::Model) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("{} is not a glTF model", source.generic_string()));
    }
    DAI_TRY_ASSIGN(const std::vector<std::filesystem::path> files, resources::ModelFiles(source));
    for (const std::filesystem::path& file : files) {
        if (file.is_absolute() || file.has_root_name() || (!file.empty() && *file.begin() == "..")) {
            return core::MakeError(
                core::ErrorCode::InvalidArgument, kModule,
                std::format("{} reads {}, outside its folder: copy the folder into the assets instead",
                            source.filename().generic_string(), file.generic_string()));
        }
    }
    const std::filesystem::path target = files.empty() ? directory : directory / source.stem();
    const std::filesystem::path model = target / source.filename();
    std::error_code error;
    if (std::filesystem::exists(model, error)) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("{} exists already: nothing was imported", model.generic_string()));
    }
    for (const std::filesystem::path& file : files) {
        if (!std::filesystem::exists(source.parent_path() / file, error)) {
            return core::MakeError(core::ErrorCode::NotFound, kModule,
                                   std::format("{} reads {}, which is missing", source.filename().generic_string(),
                                               file.generic_string()));
        }
        if (std::filesystem::exists(target / file, error)) {
            return core::MakeError(
                core::ErrorCode::InvalidArgument, kModule,
                std::format("{} exists already: nothing was imported", (target / file).generic_string()));
        }
    }
    for (const std::filesystem::path& file : files) {
        DAI_TRY(Copy(source.parent_path() / file, target / file));
    }
    DAI_TRY(Copy(source, model));
    return model;
}

core::Result<std::filesystem::path> ImportSky(const std::filesystem::path& face,
                                              const std::filesystem::path& directory) {
    const std::optional<std::string> prefix = SkyPrefix(face.filename().generic_string());
    if (!prefix) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("{} is not a face of a sky (name_right.png, _left, _up, _down, _front, "
                                           "_back)",
                                           face.filename().generic_string()));
    }
    std::error_code error;
    for (const std::string_view side : kFaces) {
        const std::string name = *prefix + std::string(side) + std::string(kSkyExtension);
        if (!std::filesystem::exists(face.parent_path() / name, error)) {
            return core::MakeError(core::ErrorCode::NotFound, kModule,
                                   std::format("The sky {} has no face {}", *prefix, name));
        }
        if (std::filesystem::exists(directory / name, error)) {
            return core::MakeError(
                core::ErrorCode::InvalidArgument, kModule,
                std::format("{} exists already: nothing was imported", (directory / name).generic_string()));
        }
    }
    for (const std::string_view side : kFaces) {
        const std::string name = *prefix + std::string(side) + std::string(kSkyExtension);
        DAI_TRY(Copy(face.parent_path() / name, directory / name));
    }
    return directory / *prefix;
}

} // namespace editor
