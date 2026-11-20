// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/ImportSettings.hpp>

#include <format>
#include <fstream>
#include <sstream>

#include "JsonTree.hpp"

namespace daidalos::scene {

using detail::Json;
using detail::JsonObject;

std::filesystem::path ImportSettingsPath(const std::filesystem::path& model) {
    std::filesystem::path path = model;
    path += ".import.json";
    return path;
}

core::Result<ModelImportSettings> ParseImportSettings(std::string_view json) {
    core::Result<Json> tree = detail::ParseJson(json, "import settings");
    if (!tree) {
        return tl::unexpected(tree.error());
    }
    const auto* object = std::get_if<JsonObject>(&tree->value);
    if (object == nullptr) {
        return core::MakeError(core::ErrorCode::ParseError, "scene", "Import settings must be a JSON object");
    }
    ModelImportSettings settings;
    for (const auto& [key, value] : *object) {
        if (key == "scale") {
            const double* scale = std::get_if<double>(&value.value);
            if (scale == nullptr || *scale <= 0.0) {
                return core::MakeError(core::ErrorCode::ParseError, "scene",
                                       "Import settings: scale must be a number above 0");
            }
            settings.scale = static_cast<float>(*scale);
        } else if (key == "level_collision") {
            const bool* collides = std::get_if<bool>(&value.value);
            if (collides == nullptr) {
                return core::MakeError(core::ErrorCode::ParseError, "scene",
                                       "Import settings: level_collision must be true or false");
            }
            settings.levelCollision = *collides;
        } else if (key != "comment") {
            return core::MakeError(core::ErrorCode::ParseError, "scene",
                                   std::format("Import settings: unknown key '{}'", key));
        }
    }
    return settings;
}

std::string WriteImportSettings(const ModelImportSettings& settings) {
    JsonObject object;
    object.emplace("scale", Json{static_cast<double>(settings.scale)});
    object.emplace("level_collision", Json{settings.levelCollision});
    return detail::WriteJson(Json{std::move(object)});
}

ModelImportSettings ReadImportSettings(const std::filesystem::path& model) {
    std::ifstream file(ImportSettingsPath(model), std::ios::binary);
    if (!file) {
        return {};
    }
    std::stringstream text;
    text << file.rdbuf();
    return ParseImportSettings(text.str()).value_or(ModelImportSettings{});
}

void ApplyImportSettings(resources::ModelData& model, const ModelImportSettings& settings) {
    if (settings.scale == 1.0f) {
        return;
    }
    for (resources::ModelNode& node : model.nodes) {
        if (!node.parent) {
            node.local.translation *= settings.scale;
            node.local.scale *= settings.scale;
        }
    }
}

} // namespace daidalos::scene
