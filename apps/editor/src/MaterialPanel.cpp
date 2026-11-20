// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "MaterialPanel.hpp"

#include <daidalos/logging/Log.hpp>
#include <daidalos/scene/GltfMaterials.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <vector>

using namespace daidalos;

namespace editor {

namespace {

constexpr std::string_view kModule = "editor";

/// What surfaces are made of, as impacts, footsteps and porosity know them (ADR-0053, ADR-0061, ADR-0067).
constexpr std::array<const char*, 10> kSurfaces{"",      "concrete", "stone", "plaster", "wood",
                                                "cloth", "flesh",    "metal", "glass",   "tile"};

std::string MaterialLabel(const scene::Model& model, std::uint32_t index) {
    const std::string& name = model.materials[index].values.name;
    return name.empty() ? std::format("Material {}", index) : std::format("{} ({})", name, index);
}

/// The fields of a material. Returns true when one changed.
bool EditValues(resources::MaterialData& values, const renderer::MaterialDesc& desc) {
    bool changed = false;
    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::ColorEdit4("Base color", &values.baseColorFactor.x);
        if (!desc.baseColorTexture.IsNull()) {
            ImGui::SameLine();
            ImGui::TextDisabled("x texture");
        }
        changed |= ImGui::SliderFloat("Metallic", &values.metallicFactor, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Roughness", &values.roughnessFactor, 0.0f, 1.0f);
        if (!desc.normalTexture.IsNull()) {
            changed |= ImGui::SliderFloat("Normal strength", &values.normalScale, 0.0f, 3.0f);
        }
        if (!desc.occlusionTexture.IsNull()) {
            changed |= ImGui::SliderFloat("Occlusion", &values.occlusionStrength, 0.0f, 1.0f);
        }
        changed |= ImGui::SliderFloat("Alpha cutoff", &values.alphaCutoff, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pixels with a lower alpha are cut out: leaves, fences. 0 keeps every pixel");
        }
        changed |= ImGui::Checkbox("Both sides", &values.doubleSided);
    }
    if (ImGui::CollapsingHeader("Emission")) {
        // A color and a strength in nits, which the file keeps as a factor and KHR_materials_emissive_strength.
        float strength = std::max({values.emissiveFactor.r, values.emissiveFactor.g, values.emissiveFactor.b});
        glm::vec3 color = strength > 0.0f ? values.emissiveFactor / strength : glm::vec3(1.0f);
        bool emission = ImGui::ColorEdit3("Emission color", &color.x);
        emission |= ImGui::DragFloat("Luminance (nits)", &strength, 1.0f, 0.0f, 100000.0f, "%.1f",
                                     ImGuiSliderFlags_Logarithmic);
        if (emission) {
            const float peak = std::max({color.r, color.g, color.b});
            values.emissiveFactor = peak > 0.0f ? color / peak * std::max(strength, 0.0f) : glm::vec3(0.0f);
            changed = true;
        }
        changed |= ImGui::Checkbox("Lights the scene", &values.emissiveLight);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Its triangles are lights (ADR-0044). Off: it glows, and lights nothing");
        }
    }
    if (ImGui::CollapsingHeader("Layers")) {
        changed |= ImGui::SliderFloat("Clear coat", &values.clearcoat, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Coat roughness", &values.clearcoatRoughness, 0.0f, 1.0f);
        changed |= ImGui::ColorEdit3("Sheen", &values.sheenColor.x);
        changed |= ImGui::SliderFloat("Sheen roughness", &values.sheenRoughness, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Glass")) {
        changed |= ImGui::SliderFloat("Transmission", &values.transmission, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Index of refraction", &values.ior, 1.0f, 2.5f);
        changed |= ImGui::DragFloat("Thickness (m)", &values.thickness, 0.001f, 0.0f, 10.0f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: thin glass, a window. Above: a closed mesh, light bends in and out of it");
        }
        changed |= ImGui::ColorEdit3("Absorption color", &values.attenuationColor.x);
        float distance = std::isfinite(values.attenuationDistance) ? values.attenuationDistance : 0.0f;
        if (ImGui::DragFloat("Absorption distance (m)", &distance, 0.01f, 0.0f, 100.0f, "%.2f")) {
            values.attenuationDistance = distance > 0.0f ? distance : std::numeric_limits<float>::infinity();
            changed = true;
        }
    }
    if (ImGui::CollapsingHeader("Water, impacts and relief", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginCombo("Made of", values.surface.empty() ? "(not said)" : values.surface.c_str())) {
            for (const char* surface : kSurfaces) {
                if (ImGui::Selectable(*surface == '\0' ? "(not said)" : surface, values.surface == surface)) {
                    values.surface = surface;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("What impacts throw up and how steps and shots sound on it");
        }
        changed |= ImGui::SliderFloat("Porosity", &values.porosity, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much water it soaks up: wet, a porous surface darkens (ADR-0061)");
        }
        changed |= ImGui::DragFloat("Relief height (m)", &values.heightScale, 0.0005f, 0.0f, 0.2f, "%.4f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Meters across the height map in the metallic-roughness alpha (ADR-0064); 0 for none");
        }
    }
    return changed;
}

} // namespace

void MaterialPanel::Show(const scene::Model* model, std::string modelPath, std::optional<std::uint32_t> material) {
    m_model = model;
    m_modelPath = std::move(modelPath);
    if (material) {
        m_index = *material;
    } else if (model == nullptr || m_index >= model->materials.size()) {
        m_index = 0;
    }
}

void MaterialPanel::Clear() {
    m_model = nullptr;
    m_modelPath.clear();
}

void MaterialPanel::Forget() {
    Clear();
    m_changed.clear();
}

bool MaterialPanel::Dirty() const {
    return !m_changed.empty();
}

bool MaterialPanel::ModelDirty(const scene::Model* model) const {
    return std::ranges::any_of(m_changed, [model](const auto& entry) { return entry.first.first == model; });
}

bool MaterialPanel::Save(const std::filesystem::path& assetRoot, const scene::Model* model) {
    std::vector<scene::MaterialEdit> edits;
    std::string path;
    for (const auto& [key, changed] : m_changed) {
        if (key.first == model) {
            edits.push_back({.index = key.second, .values = model->materials[key.second].values});
            path = changed.modelPath;
        }
    }
    if (edits.empty()) {
        return true;
    }
    if (core::Result<void> saved = scene::SaveGltfMaterials(assetRoot / std::filesystem::path(path), edits); !saved) {
        m_status = core::Format(saved.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
        return false;
    }
    std::erase_if(m_changed, [model](const auto& entry) { return entry.first.first == model; });
    m_status = std::format("Saved {} materials into {}", edits.size(), path);
    DAI_LOG_INFO(kModule, "{}", m_status);
    return true;
}

bool MaterialPanel::SaveAll(const std::filesystem::path& assetRoot) {
    std::vector<const scene::Model*> models;
    for (const auto& [key, changed] : m_changed) {
        if (std::ranges::find(models, key.first) == models.end()) {
            models.push_back(key.first);
        }
    }
    bool saved = true;
    for (const scene::Model* model : models) {
        saved &= Save(assetRoot, model);
    }
    return saved;
}

void MaterialPanel::Revert(scene::AssetLibrary& assets, const scene::Model* model) {
    for (auto entry = m_changed.begin(); entry != m_changed.end();) {
        if (entry->first.first == model) {
            m_edits.push_back(Edit{.key = entry->first,
                                   .before = model->materials[entry->first.second].values,
                                   .after = entry->second.saved});
            assets.UpdateMaterial(*model, entry->first.second, entry->second.saved);
            entry = m_changed.erase(entry);
        } else {
            ++entry;
        }
    }
    m_status = "Put back the materials as the file has them";
}

void MaterialPanel::Restore(scene::AssetLibrary& assets, const MaterialKey& key,
                            const resources::MaterialData& values) {
    const scene::Model* model = key.first;
    if (model == nullptr || key.second >= model->materials.size() ||
        SameValues(model->materials[key.second].values, values)) {
        return;
    }
    if (!m_changed.contains(key)) {
        m_changed.emplace(key, Changed{model->path, model->materials[key.second].values});
    }
    assets.UpdateMaterial(*model, key.second, values);
    if (SameValues(values, m_changed.at(key).saved)) {
        m_changed.erase(key);
    }
}

void MaterialPanel::Draw(scene::AssetLibrary& assets, const std::filesystem::path& assetRoot) {
    if (!ImGui::Begin("Materials")) {
        ImGui::End();
        return;
    }
    if (m_model == nullptr || m_model->materials.empty()) {
        ImGui::TextWrapped(m_model == nullptr ? "Select something that places a model, or click a surface in the view."
                                              : "This model has no materials of its own.");
        if (Dirty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "Changed materials of other models: Ctrl+S saves them.");
        }
        ImGui::End();
        return;
    }
    const scene::Model& model = *m_model;
    m_index = std::min<std::uint32_t>(m_index, static_cast<std::uint32_t>(model.materials.size() - 1));
    ImGui::TextDisabled("%s", m_modelPath.c_str());
    if (ImGui::BeginCombo("Material", MaterialLabel(model, m_index).c_str())) {
        for (std::uint32_t index = 0; index < model.materials.size(); ++index) {
            const bool changed = m_changed.contains({m_model, index});
            const std::string label = MaterialLabel(model, index) + (changed ? " *" : "");
            if (ImGui::Selectable(label.c_str(), index == m_index)) {
                m_index = index;
            }
        }
        ImGui::EndCombo();
    }

    resources::MaterialData values = model.materials[m_index].values;
    if (EditValues(values, model.materials[m_index].desc)) {
        const Key key{m_model, m_index};
        if (!m_changed.contains(key)) {
            m_changed.emplace(key, Changed{m_modelPath, model.materials[m_index].values});
        }
        m_edits.push_back(Edit{.key = key, .before = model.materials[m_index].values, .after = values});
        assets.UpdateMaterial(model, m_index, values);
        // Back where the file has it: nothing to save.
        if (SameValues(values, m_changed.at(key).saved)) {
            m_changed.erase(key);
        }
    }

    ImGui::Separator();
    const bool dirty = ModelDirty(m_model);
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save into the model")) {
        Save(assetRoot, m_model);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        Revert(assets, m_model);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(dirty ? "changed" : "as saved");
    ImGui::TextWrapped("Every entity placing this model shows the change at once. Saving rewrites the glTF file "
                       "with its keys sorted.");
    if (!m_status.empty()) {
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::End();
}

} // namespace editor
