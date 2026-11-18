// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/GltfMaterials.hpp>

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

#include "JsonTree.hpp"

namespace daidalos::scene {

namespace {

using detail::Json;
using detail::JsonArray;
using detail::JsonObject;

constexpr std::string_view kModule = "scene";

/// The material extensions this writer owns: taken out of extensionsUsed when no material uses them any more.
constexpr std::array<std::string_view, 6> kOwned{
    "KHR_materials_emissive_strength", "KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_volume",
    "KHR_materials_clearcoat",         "KHR_materials_sheen"};

tl::unexpected<core::Error> Invalid(std::string_view what) {
    return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("glTF materials: {}", what));
}

Json Number(float value) {
    return Json{static_cast<double>(value)};
}

Json Numbers(std::initializer_list<float> values) {
    JsonArray array;
    for (const float value : values) {
        array.push_back(Number(value));
    }
    return Json{std::move(array)};
}

JsonObject& ObjectAt(JsonObject& parent, std::string_view key) {
    auto found = parent.find(key);
    if (found == parent.end() || !std::holds_alternative<JsonObject>(found->second.value)) {
        found = parent.insert_or_assign(std::string(key), Json{JsonObject{}}).first;
    }
    return std::get<JsonObject>(found->second.value);
}

/// Sets the keys of an extension, or takes them out when off; the extension goes when nothing is left in it.
void Extension(JsonObject& material, std::string_view name, bool on,
               std::initializer_list<std::pair<std::string_view, Json>> values) {
    JsonObject& extensions = ObjectAt(material, "extensions");
    JsonObject& extension = ObjectAt(extensions, name);
    for (const auto& [key, value] : values) {
        if (on) {
            extension.insert_or_assign(std::string(key), value);
        } else {
            extension.erase(std::string(key));
        }
    }
    if (extension.empty()) {
        extensions.erase(std::string(name));
    }
    if (extensions.empty()) {
        material.erase("extensions");
    }
}

void Write(JsonObject& material, const resources::MaterialData& values) {
    JsonObject& pbr = ObjectAt(material, "pbrMetallicRoughness");
    const glm::vec4& color = values.baseColorFactor;
    pbr.insert_or_assign("baseColorFactor", Numbers({color.r, color.g, color.b, color.a}));
    pbr.insert_or_assign("metallicFactor", Number(values.metallicFactor));
    pbr.insert_or_assign("roughnessFactor", Number(values.roughnessFactor));

    // Emission: a color in [0, 1] times a strength, which only KHR_materials_emissive_strength takes over 1.
    const float strongest = std::max({values.emissiveFactor.r, values.emissiveFactor.g, values.emissiveFactor.b});
    const float strength = strongest > 1.0f ? strongest : 1.0f;
    const glm::vec3 emissive = values.emissiveFactor / strength;
    if (strongest > 0.0f) {
        material.insert_or_assign("emissiveFactor", Numbers({emissive.r, emissive.g, emissive.b}));
    } else {
        material.erase("emissiveFactor");
    }
    Extension(material, "KHR_materials_emissive_strength", strength > 1.0f, {{"emissiveStrength", Number(strength)}});

    if (const auto normal = material.find("normalTexture"); normal != material.end()) {
        if (auto* texture = std::get_if<JsonObject>(&normal->second.value)) {
            texture->insert_or_assign("scale", Number(values.normalScale));
        }
    }
    if (const auto occlusion = material.find("occlusionTexture"); occlusion != material.end()) {
        if (auto* texture = std::get_if<JsonObject>(&occlusion->second.value)) {
            texture->insert_or_assign("strength", Number(values.occlusionStrength));
        }
    }

    if (values.alphaCutoff > 0.0f) {
        material.insert_or_assign("alphaMode", Json{std::string("MASK")});
        material.insert_or_assign("alphaCutoff", Number(values.alphaCutoff));
    } else if (const auto mode = material.find("alphaMode"); mode != material.end() &&
                                                             std::holds_alternative<std::string>(mode->second.value) &&
                                                             std::get<std::string>(mode->second.value) == "MASK") {
        material.erase("alphaMode");
        material.erase("alphaCutoff");
    }
    if (values.doubleSided) {
        material.insert_or_assign("doubleSided", Json{true});
    } else {
        material.erase("doubleSided");
    }

    Extension(material, "KHR_materials_transmission", values.transmission > 0.0f,
              {{"transmissionFactor", Number(values.transmission)}});
    Extension(material, "KHR_materials_ior", values.ior != 1.5f, {{"ior", Number(values.ior)}});
    const glm::vec3& attenuation = values.attenuationColor;
    Extension(material, "KHR_materials_volume", values.thickness > 0.0f,
              {{"thicknessFactor", Number(values.thickness)},
               {"attenuationColor", Numbers({attenuation.r, attenuation.g, attenuation.b})}});
    Extension(material, "KHR_materials_volume", values.thickness > 0.0f && std::isfinite(values.attenuationDistance),
              {{"attenuationDistance", Number(values.attenuationDistance)}});
    Extension(material, "KHR_materials_clearcoat", values.clearcoat > 0.0f,
              {{"clearcoatFactor", Number(values.clearcoat)},
               {"clearcoatRoughnessFactor", Number(values.clearcoatRoughness)}});
    const glm::vec3& sheen = values.sheenColor;
    Extension(material, "KHR_materials_sheen", glm::any(glm::greaterThan(sheen, glm::vec3(0.0f))),
              {{"sheenColorFactor", Numbers({sheen.r, sheen.g, sheen.b})},
               {"sheenRoughnessFactor", Number(values.sheenRoughness)}});

    // What only the engine reads (ADR-0044, ADR-0053, ADR-0061, ADR-0064).
    JsonObject& extras = ObjectAt(material, "extras");
    const auto extra = [&extras](std::string_view key, bool on, Json value) {
        if (on) {
            extras.insert_or_assign(std::string(key), std::move(value));
        } else {
            extras.erase(std::string(key));
        }
    };
    extra("light", !values.emissiveLight, Json{false});
    extra("surface", !values.surface.empty(), Json{values.surface});
    extra("porosity", true, Number(values.porosity));
    extra("height", values.heightScale > 0.0f, Number(values.heightScale));
    if (extras.empty()) {
        material.erase("extras");
    }
}

} // namespace

core::Result<std::string> WriteGltfMaterials(std::string_view gltf, std::span<const MaterialEdit> edits) {
    DAI_TRY_ASSIGN(Json document, detail::ParseJson(gltf, "The glTF file"));
    auto* root = std::get_if<JsonObject>(&document.value);
    if (root == nullptr || !root->contains("asset")) {
        return Invalid("not a glTF document");
    }
    const auto materials = root->find("materials");
    auto* list = materials != root->end() ? std::get_if<JsonArray>(&materials->second.value) : nullptr;
    for (const MaterialEdit& edit : edits) {
        if (list == nullptr || edit.index >= list->size() ||
            !std::holds_alternative<JsonObject>((*list)[edit.index].value)) {
            return Invalid(std::format("there is no material {}", edit.index));
        }
        Write(std::get<JsonObject>((*list)[edit.index].value), edit.values);
    }

    // extensionsUsed: those of ours some material still uses, and the others as they were.
    std::set<std::string> used;
    if (list != nullptr) {
        for (const Json& material : *list) {
            const auto* object = std::get_if<JsonObject>(&material.value);
            if (object == nullptr) {
                continue;
            }
            const auto extensions = object->find("extensions");
            if (extensions == object->end()) {
                continue;
            }
            if (const auto* names = std::get_if<JsonObject>(&extensions->second.value)) {
                for (const auto& [name, value] : *names) {
                    used.insert(name);
                }
            }
        }
    }
    JsonArray declared;
    std::set<std::string> listed;
    if (const auto found = root->find("extensionsUsed"); found != root->end()) {
        if (const auto* names = std::get_if<JsonArray>(&found->second.value)) {
            for (const Json& name : *names) {
                const auto* text = std::get_if<std::string>(&name.value);
                if (text == nullptr) {
                    continue;
                }
                const bool ours = std::ranges::find(kOwned, *text) != kOwned.end();
                if (!ours || used.contains(*text)) {
                    declared.push_back(name);
                    listed.insert(*text);
                }
            }
        }
    }
    for (const std::string_view name : kOwned) {
        if (used.contains(std::string(name)) && !listed.contains(std::string(name))) {
            declared.push_back(Json{std::string(name)});
        }
    }
    if (declared.empty()) {
        root->erase("extensionsUsed");
    } else {
        root->insert_or_assign("extensionsUsed", Json{std::move(declared)});
    }
    return detail::WriteJson(document);
}

core::Result<void> SaveGltfMaterials(const std::filesystem::path& file, std::span<const MaterialEdit> edits) {
    std::string extension = file.extension().generic_string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".gltf") {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("Only .gltf files take materials back, not {}", file.generic_string()));
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return core::MakeError(core::ErrorCode::NotFound, kModule,
                               std::format("Cannot read {}", file.generic_string()));
    }
    std::ostringstream text;
    text << stream.rdbuf();
    stream.close();
    core::Result<std::string> written = WriteGltfMaterials(text.str(), edits);
    if (!written) {
        core::Error error = written.error();
        error.message = std::format("{}: {}", file.generic_string(), error.message);
        return tl::unexpected(std::move(error));
    }
    std::filesystem::path temporary = file;
    temporary += ".saving";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(written->data(), static_cast<std::streamsize>(written->size()));
        if (!out) {
            return core::MakeError(core::ErrorCode::IoError, kModule,
                                   std::format("Cannot write {}", temporary.generic_string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot replace {}: {}", file.generic_string(), error.message()));
    }
    return {};
}

} // namespace daidalos::scene
