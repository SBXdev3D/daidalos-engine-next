// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/AssetLibrary.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "History.hpp"

namespace editor {

/// The material panel (ADR-0079): the materials of the model under the selection, their values edited with the
/// scene showing each change as it is made, and saved back into the model's glTF file. Its edits are steps of the
/// editor's history (ADR-0109): the editor takes them each frame, and puts values back through Restore.
class MaterialPanel {
public:
    /// Shows a model's materials, and one of them if given. modelPath is the model's file under the asset root.
    void Show(const daidalos::scene::Model* model, std::string modelPath, std::optional<std::uint32_t> material);
    /// Nothing to show: the selection places no model.
    void Clear();
    /// A model is about to be unloaded (another level opens): what was not saved is lost.
    void Forget();

    void Draw(daidalos::scene::AssetLibrary& assets, const std::filesystem::path& assetRoot);

    /// A change of a material's values in the panel: Revert's or a field's.
    struct Edit {
        MaterialKey key;
        daidalos::resources::MaterialData before;
        daidalos::resources::MaterialData after;
    };
    /// The changes made since the last call.
    [[nodiscard]] std::vector<Edit> TakeEdits() { return std::exchange(m_edits, {}); }
    /// Gives a material values again, as undo or redo does, keeping track of whether it differs from its file.
    void Restore(daidalos::scene::AssetLibrary& assets, const MaterialKey& key,
                 const daidalos::resources::MaterialData& values);

    /// Whether a material differs from its file.
    [[nodiscard]] bool Dirty() const;
    /// Writes every changed material into its model's file. Returns false if one failed; Status says why.
    bool SaveAll(const std::filesystem::path& assetRoot);
    [[nodiscard]] const std::string& Status() const { return m_status; }

private:
    using Key = MaterialKey;
    struct Changed {
        std::string modelPath;
        /// The values its file holds.
        daidalos::resources::MaterialData saved;
    };
    bool Save(const std::filesystem::path& assetRoot, const daidalos::scene::Model* model);
    void Revert(daidalos::scene::AssetLibrary& assets, const daidalos::scene::Model* model);
    [[nodiscard]] bool ModelDirty(const daidalos::scene::Model* model) const;

    const daidalos::scene::Model* m_model = nullptr;
    std::string m_modelPath;
    std::uint32_t m_index = 0;
    /// The materials changed since they were read or saved.
    std::map<Key, Changed> m_changed;
    std::vector<Edit> m_edits;
    std::string m_status;
};

} // namespace editor
