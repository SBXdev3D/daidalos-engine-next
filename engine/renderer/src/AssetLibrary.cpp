// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/renderer/AssetLibrary.hpp>
#include <daidalos/resources/AssetPath.hpp>

#include <format>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";

std::string_view ToString(resources::TextureUse use) {
    switch (use) {
    case resources::TextureUse::Color:
        return "color";
    case resources::TextureUse::Data:
        return "data";
    case resources::TextureUse::Normal:
        return "normal";
    }
    return "";
}

} // namespace

AssetLibrary::AssetLibrary(Renderer& renderer, std::filesystem::path root)
    : m_renderer(&renderer), m_root(std::move(root)) {}

AssetLibrary::~AssetLibrary() {
    // Materials first: they refer to the textures.
    for (const MaterialHandle material : m_materials) {
        m_renderer->DestroyMaterial(material);
    }
    for (const MeshHandle mesh : m_meshes) {
        m_renderer->DestroyMesh(mesh);
    }
    m_textures.ForEach([this](rhi::TextureHandle texture) { m_renderer->Device().DestroyTexture(texture); });
}

core::Result<rhi::TextureHandle> AssetLibrary::LoadTexture(const resources::ImageSource& source,
                                                           const std::string& modelKey, std::uint32_t imageIndex) {
    std::string identity;
    std::filesystem::path file;
    if (source.file.empty()) {
        identity = std::format("{}#{}", modelKey, imageIndex);
    } else {
        // The identity of a file comes from the root, whichever model refers to it.
        core::Result<std::string> normalized =
            resources::NormalizeAssetPath(resources::ToUtf8(source.file.lexically_relative(m_root)));
        if (!normalized) {
            return tl::unexpected(std::move(normalized.error()));
        }
        core::Result<std::filesystem::path> resolved = resources::ResolveAssetPath(m_root, *normalized);
        if (!resolved) {
            return tl::unexpected(std::move(resolved.error()));
        }
        identity = std::move(*normalized);
        file = std::move(*resolved);
    }

    // A file used in two ways is two textures: the use decides how it is decoded.
    std::string key = std::format("{}|{}", identity, ToString(source.use));
    if (const rhi::TextureHandle* found = m_textures.Find(key)) {
        return *found;
    }

    core::Result<resources::ImageData> image = file.empty()
                                                   ? resources::DecodeImage(source.embedded, source.use, identity)
                                                   : resources::LoadImageFile(file, source.use);
    if (!image) {
        return tl::unexpected(std::move(image.error()));
    }
    core::Result<rhi::TextureHandle> texture = CreateTexture(m_renderer->Device(), *image, identity);
    if (!texture) {
        return texture;
    }
    m_textures.Insert(std::move(key), *texture);
    return texture;
}

core::Result<Model> AssetLibrary::BuildModel(const resources::ModelData& data, const std::string& key) {
    std::vector<rhi::TextureHandle> textures;
    textures.reserve(data.images.size());
    for (std::uint32_t index = 0; index < data.images.size(); ++index) {
        core::Result<rhi::TextureHandle> texture = LoadTexture(data.images[index], key, index);
        if (!texture) {
            return tl::unexpected(std::move(texture.error()));
        }
        textures.push_back(*texture);
    }
    const auto textureOf = [&textures](const std::optional<std::uint32_t>& image) {
        return image.has_value() ? textures[*image] : rhi::TextureHandle{};
    };

    // What is created on the way stays in the library, also if a later step fails.
    std::vector<MaterialHandle> materials;
    materials.reserve(data.materials.size());
    for (const resources::MaterialData& material : data.materials) {
        MaterialDesc desc;
        desc.baseColorFactor = material.baseColorFactor;
        desc.emissiveFactor = material.emissiveFactor;
        desc.metallicFactor = material.metallicFactor;
        desc.roughnessFactor = material.roughnessFactor;
        desc.normalScale = material.normalScale;
        desc.occlusionStrength = material.occlusionStrength;
        desc.alphaCutoff = material.alphaCutoff;
        desc.doubleSided = material.doubleSided;
        desc.baseColorTexture = textureOf(material.baseColorImage);
        desc.metallicRoughnessTexture = textureOf(material.metallicRoughnessImage);
        desc.normalTexture = textureOf(material.normalImage);
        desc.occlusionTexture = textureOf(material.occlusionImage);
        desc.emissiveTexture = textureOf(material.emissiveImage);
        core::Result<MaterialHandle> created = m_renderer->CreateMaterial(desc);
        if (!created) {
            return tl::unexpected(std::move(created.error()));
        }
        m_materials.push_back(*created);
        materials.push_back(*created);
    }

    std::vector<Model::Part> parts;
    parts.reserve(data.primitives.size());
    for (std::size_t index = 0; index < data.primitives.size(); ++index) {
        const resources::ModelPrimitive& primitive = data.primitives[index];
        core::Result<MeshHandle> mesh = m_renderer->CreateMesh(primitive.mesh, std::format("{} #{}", key, index));
        if (!mesh) {
            return tl::unexpected(std::move(mesh.error()));
        }
        m_meshes.push_back(*mesh);
        parts.push_back({*mesh, primitive.material.has_value() ? materials[*primitive.material] : MaterialHandle{}});
    }

    Model model;
    model.nodes.reserve(data.nodes.size());
    for (const resources::ModelNode& node : data.nodes) {
        Model::Node& added = model.nodes.emplace_back();
        added.name = node.name;
        added.parent = node.parent;
        added.local = node.local;
        added.parts.reserve(node.primitives.size());
        for (const std::uint32_t primitive : node.primitives) {
            added.parts.push_back(parts[primitive]);
        }
    }
    return model;
}

core::Result<const Model*> AssetLibrary::LoadModel(std::string_view path) {
    DAI_PROFILE_SCOPE("renderer::LoadModel");
    core::Result<std::string> key = resources::NormalizeAssetPath(path);
    if (!key) {
        return tl::unexpected(std::move(key.error()));
    }
    if (const Model* found = m_models.Find(*key)) {
        return found;
    }

    core::Result<std::filesystem::path> file = resources::ResolveAssetPath(m_root, *key);
    if (!file) {
        return tl::unexpected(std::move(file.error()));
    }
    core::Result<resources::ModelData> data = resources::LoadModelFile(*file);
    if (!data) {
        return tl::unexpected(std::move(data.error()));
    }
    core::Result<Model> model = BuildModel(*data, *key);
    if (!model) {
        return tl::unexpected(std::move(model.error()));
    }
    DAI_LOG_INFO(kModule, "Loaded model '{}': {} meshes, {} materials, {} images", *key, data->primitives.size(),
                 data->materials.size(), data->images.size());
    return &m_models.Insert(std::move(*key), std::move(*model));
}

core::Result<ecs::Entity> Instantiate(ecs::World& world, const Model& model) {
    const ecs::Entity root = world.Create();
    world.Add<ecs::LocalTransform>(root);

    std::vector<ecs::Entity> entities;
    entities.reserve(model.nodes.size());
    for (const Model::Node& node : model.nodes) {
        const ecs::Entity entity = world.Create();
        ecs::LocalTransform transform;
        static_cast<math::Transform&>(transform) = node.local;
        world.Add<ecs::LocalTransform>(entity, transform);
        const ecs::Entity parent = node.parent.has_value() ? entities[*node.parent] : root;
        if (core::Result<void> attached = world.SetParent(entity, parent); !attached) {
            return tl::unexpected(std::move(attached.error()));
        }
        entities.push_back(entity);

        for (std::size_t index = 0; index < node.parts.size(); ++index) {
            const Model::Part& part = node.parts[index];
            ecs::Entity holder = entity;
            if (index > 0) {
                holder = world.Create();
                world.Add<ecs::LocalTransform>(holder);
                if (core::Result<void> attached = world.SetParent(holder, entity); !attached) {
                    return tl::unexpected(std::move(attached.error()));
                }
            }
            world.Add<MeshRenderer>(holder, part.mesh, part.material);
        }
    }
    return root;
}

} // namespace daidalos::renderer
