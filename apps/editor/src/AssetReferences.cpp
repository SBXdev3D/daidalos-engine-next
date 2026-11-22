// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "AssetReferences.hpp"

#include <daidalos/resources/ModelData.hpp>
#include <daidalos/scene/EffectDocument.hpp>
#include <daidalos/scene/ImportSettings.hpp>
#include <daidalos/scene/ParticleEffectFile.hpp>
#include <daidalos/scene/Sequence.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

using namespace daidalos;

namespace editor {

namespace {

std::string Read(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

/// A path relative to a file's directory, as a path under the root.
std::string Under(const std::string& file, const std::string& path) {
    return (std::filesystem::path(file).parent_path() / path).lexically_normal().generic_string();
}

/// A path under the root, relative to a file's directory.
std::string RelativeTo(const std::string& file, const std::string& path) {
    std::filesystem::path directory = std::filesystem::path(file).parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    return std::filesystem::path(path).lexically_relative(directory).generic_string();
}

/// Every path field of a level, with what it is, for reading and repointing alike.
void EachPath(scene::Level& level, const std::function<void(std::string& path, const std::string& what)>& visit) {
    visit(level.environment.skyFaces, "the sky");
    for (scene::LevelEntity& entity : level.entities) {
        const std::string name = entity.name.empty() ? std::format("entity {}", entity.id) : entity.name;
        visit(entity.model, name + "'s model");
        visit(entity.prefab, name + "'s prefab");
        if (entity.trigger) {
            visit(entity.trigger->sequence, name + "'s sequence");
        }
        if (entity.light) {
            visit(entity.light->profile, name + "'s light profile");
            visit(entity.light->cookie, name + "'s light cookie");
        }
        if (entity.decal) {
            visit(entity.decal->texture, name + "'s decal");
            visit(entity.decal->normal, name + "'s decal normal map");
        }
        if (entity.sound) {
            visit(entity.sound->clip, name + "'s sound");
        }
        if (entity.particles) {
            visit(entity.particles->effect, name + "'s effect");
        }
        if (entity.attractor) {
            visit(entity.attractor->effect, name + "'s effect");
        }
        if (entity.weather) {
            visit(entity.weather->effect, name + "'s effect");
        }
        if (entity.ribbon) {
            visit(entity.ribbon->effect, name + "'s ribbon");
        }
        for (scene::PrefabOverride& change : entity.overrides) {
            if (change.model) {
                visit(*change.model, name + "'s part's model");
            }
        }
    }
}

/// The string keys of an effect file that name other files, relative to it.
constexpr std::array<const char*, 4> kEffectPaths{"texture", "mesh", "onDeath.effect", "onHit.effect"};

/// Each file under the root a reference may be in, by its path under the root.
void EachFile(const std::filesystem::path& root, const std::function<void(const std::string& file)>& visit) {
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator(root, error);
         !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
        if (entry->is_directory(error) && entry->path().filename() == "Source") {
            entry.disable_recursion_pending();
            continue;
        }
        const std::string name = entry->path().filename().generic_string();
        if (entry->is_regular_file(error) &&
            (name.ends_with(".level.json") || name.ends_with(".prefab.json") || name.ends_with(".effect.json") ||
             name.ends_with(".ribbon.json") || name.ends_with(".sequence.json")) &&
            !name.ends_with(".play.level.json")) {
            visit(entry->path().lexically_relative(root).generic_string());
        }
    }
}

} // namespace

std::size_t RepointLevel(scene::Level& level, const std::string& levelPath, const std::string& from,
                         const std::string& to) {
    std::size_t changed = 0;
    EachPath(level, [&](std::string& path, const std::string& /*what*/) {
        if (!path.empty() && Under(levelPath, path) == from) {
            path = RelativeTo(levelPath, to);
            ++changed;
        }
    });
    return changed;
}

std::vector<AssetReference> FindReferences(const std::filesystem::path& root, const std::string& asset) {
    std::vector<AssetReference> references;
    EachFile(root, [&](const std::string& file) {
        const std::string text = Read(root / file);
        if (file.ends_with(".level.json") || file.ends_with(".prefab.json")) {
            core::Result<scene::Level> level = scene::ParseLevel(text);
            if (!level) {
                return;
            }
            EachPath(*level, [&](std::string& path, const std::string& what) {
                if (!path.empty() && Under(file, path) == asset) {
                    references.push_back({file, what});
                }
            });
        } else if (file.ends_with(".sequence.json")) {
            if (core::Result<scene::Sequence> sequence = scene::ParseSequence(text, file)) {
                for (const scene::Cue& cue : sequence->cues) {
                    if (!cue.path.empty() && cue.path == asset) {
                        references.push_back({file, std::format("a cue at {:.2f} s", cue.at)});
                    }
                }
            }
        } else if (core::Result<scene::EffectDocument> effect = scene::EffectDocument::Parse(text)) {
            for (const char* key : kEffectPaths) {
                if (const std::optional<std::string> path = effect->Text(key); path && Under(file, *path) == asset) {
                    references.push_back({file, std::format("its {}", key)});
                }
            }
        }
    });
    // By file: the order the folders are walked in is the file system's, which differs between them.
    std::ranges::stable_sort(references, {}, &AssetReference::file);
    return references;
}

core::Result<std::vector<std::string>> MoveAsset(const std::filesystem::path& root, const std::string& from,
                                                 const std::string& to, const std::string& skip) {
    const std::filesystem::path source = root / from;
    const std::filesystem::path target = root / to;
    std::error_code error;
    if (from == to) {
        return std::vector<std::string>{};
    }
    if (!std::filesystem::is_regular_file(source, error)) {
        return core::MakeError(core::ErrorCode::NotFound, "editor", std::format("No file {}", from));
    }
    if (std::filesystem::exists(target, error)) {
        return core::MakeError(core::ErrorCode::InvalidArgument, "editor", std::format("{} exists already", to));
    }
    const std::string name = std::filesystem::path(from).filename().generic_string();
    if ((name.ends_with(".gltf") || name.ends_with(".glb")) && source.parent_path() != target.parent_path()) {
        if (core::Result<std::vector<std::filesystem::path>> files = resources::ModelFiles(source);
            files && !files->empty()) {
            return core::MakeError(core::ErrorCode::InvalidArgument, "editor",
                                   std::format("{} reads {} files beside it: move its folder instead, or rename it "
                                               "where it is",
                                               from, files->size()));
        }
    }
    // What names it, read before it moves.
    std::vector<AssetReference> references = FindReferences(root, from);
    std::filesystem::create_directories(target.parent_path(), error);
    std::filesystem::rename(source, target, error);
    if (error) {
        return core::MakeError(core::ErrorCode::IoError, "editor",
                               std::format("Cannot move {} to {}: {}", from, to, error.message()));
    }
    if (std::filesystem::exists(scene::ImportSettingsPath(source), error)) {
        std::filesystem::rename(scene::ImportSettingsPath(source), scene::ImportSettingsPath(target), error);
    }
    std::vector<std::string> rewritten;
    std::ranges::sort(references, {}, &AssetReference::file);
    for (auto reference = references.begin(); reference != references.end();) {
        const std::string file = reference->file;
        reference = std::ranges::find_if(reference, references.end(),
                                         [&file](const AssetReference& other) { return other.file != file; });
        if (file == skip) {
            continue;
        }
        const std::filesystem::path path = root / file;
        const std::string text = Read(path);
        if (file.ends_with(".level.json") || file.ends_with(".prefab.json")) {
            core::Result<scene::Level> level = scene::ParseLevel(text);
            if (level && RepointLevel(*level, file, from, to) > 0) {
                if (scene::SaveLevelFile(path, *level)) {
                    rewritten.push_back(file);
                }
            }
        } else if (file.ends_with(".sequence.json")) {
            if (core::Result<scene::Sequence> sequence = scene::ParseSequence(text, file)) {
                for (scene::Cue& cue : sequence->cues) {
                    if (cue.path == from) {
                        cue.path = to;
                    }
                }
                std::ofstream(path, std::ios::binary) << scene::WriteSequence(*sequence);
                rewritten.push_back(file);
            }
        } else if (core::Result<scene::EffectDocument> effect = scene::EffectDocument::Parse(text)) {
            for (const char* key : kEffectPaths) {
                if (const std::optional<std::string> named = effect->Text(key); named && Under(file, *named) == from) {
                    effect->SetText(key, RelativeTo(file, to));
                }
            }
            std::ofstream(path, std::ios::binary) << effect->Write();
            rewritten.push_back(file);
        }
    }
    return rewritten;
}

} // namespace editor
