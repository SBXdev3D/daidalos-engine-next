// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

// Materials written back into the glTF files they come from, by the editor's material panel (ADR-0079).
namespace daidalos::scene {

/// A material's new values, for the material of that index in its glTF file.
struct MaterialEdit {
    std::uint32_t index = 0;
    resources::MaterialData values;
};

/// The glTF document with the materials' values replaced: the core factors, alpha, double-sidedness, the
/// KHR_materials_* extensions the engine reads, and the extras {"light", "surface", "porosity", "height"}. An
/// extension at its default is taken out unless it holds a texture; extensionsUsed follows. Textures, names
/// and everything else stay as read, written with sorted keys. Emission over 1 goes into
/// KHR_materials_emissive_strength. A missing material or a document that is not glTF JSON is an error.
[[nodiscard]] core::Result<std::string> WriteGltfMaterials(std::string_view gltf, std::span<const MaterialEdit> edits);

/// Reads a .gltf file, writes the materials into it, and replaces it through a temporary file. A .glb file is
/// refused: its JSON sits in a binary container the editor does not rewrite.
[[nodiscard]] core::Result<void> SaveGltfMaterials(const std::filesystem::path& file,
                                                   std::span<const MaterialEdit> edits);

} // namespace daidalos::scene
