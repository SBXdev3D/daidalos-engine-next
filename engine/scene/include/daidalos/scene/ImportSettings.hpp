// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace daidalos::scene {

/// How a model is taken in (ADR-0117), kept beside it as `<file>.import.json`, so that the file itself stays as
/// Blender wrote it.
struct ModelImportSettings {
    /// Multiplies the model's size: 0.01 for a model made in centimeters.
    float scale = 1.0f;
    /// The game makes every part of it walls and floors, as the parts of nodes named `level` (ADR-0083).
    bool levelCollision = false;
    [[nodiscard]] bool operator==(const ModelImportSettings&) const = default;
};

/// The settings' file of a model.
[[nodiscard]] std::filesystem::path ImportSettingsPath(const std::filesystem::path& model);
[[nodiscard]] core::Result<ModelImportSettings> ParseImportSettings(std::string_view json);
[[nodiscard]] std::string WriteImportSettings(const ModelImportSettings& settings);
/// The settings beside a model, or the defaults when it has none or they cannot be read.
[[nodiscard]] ModelImportSettings ReadImportSettings(const std::filesystem::path& model);
/// Scales a model's root nodes, which scales everything under them.
void ApplyImportSettings(resources::ModelData& model, const ModelImportSettings& settings);

} // namespace daidalos::scene
