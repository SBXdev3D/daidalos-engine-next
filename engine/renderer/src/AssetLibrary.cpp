// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/renderer/AssetLibrary.hpp>
#include <daidalos/resources/AssetPath.hpp>

#include <format>
#include <memory>
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
    for (const PendingLoad& load : m_pending) {
        load.workers->Cancel(load.job);
    }
    // Materials first: they refer to the textures.
    for (const MaterialHandle material : m_materials) {
        m_renderer->DestroyMaterial(material);
    }
    for (const MeshHandle mesh : m_meshes) {
        m_renderer->DestroyMesh(mesh);
    }
    m_textures.ForEach([this](rhi::TextureHandle texture) { m_renderer->Device().DestroyTexture(texture); });
}

core::Result<AssetLibrary::DecodedModel> AssetLibrary::Decode(const std::filesystem::path& root,
                                                              const std::string& key) {
    DAI_PROFILE_SCOPE("renderer::DecodeModel");
    core::Result<std::filesystem::path> file = resources::ResolveAssetPath(root, key);
    if (!file) {
        return tl::unexpected(std::move(file.error()));
    }
    core::Result<resources::ModelData> data = resources::LoadModelFile(*file);
    if (!data) {
        return tl::unexpected(std::move(data.error()));
    }

    DecodedModel decoded{std::move(*data), {}};
    decoded.images.reserve(decoded.data.images.size());
    for (std::uint32_t index = 0; index < decoded.data.images.size(); ++index) {
        const resources::ImageSource& source = decoded.data.images[index];
        if (source.file.empty()) {
            std::string identity = std::format("{}#{}", key, index);
            std::string textureKey = std::format("{}|{}", identity, ToString(source.use));
            core::Result<resources::ImageData> image = resources::DecodeImage(source.embedded, source.use, identity);
            decoded.images.push_back({std::move(identity), std::move(textureKey), std::move(image)});
            continue;
        }
        // The identity of a file comes from the root, whichever model refers to it.
        core::Result<std::string> normalized =
            resources::NormalizeAssetPath(resources::ToUtf8(source.file.lexically_relative(root)));
        if (!normalized) {
            return tl::unexpected(std::move(normalized.error()));
        }
        core::Result<std::filesystem::path> resolved = resources::ResolveAssetPath(root, *normalized);
        if (!resolved) {
            return tl::unexpected(std::move(resolved.error()));
        }
        // A file used in two ways is two textures: the use decides how it is decoded.
        std::string textureKey = std::format("{}|{}", *normalized, ToString(source.use));
        decoded.images.push_back(
            {std::move(*normalized), std::move(textureKey), resources::LoadImageFile(*resolved, source.use)});
    }
    return decoded;
}

core::Result<rhi::TextureHandle> AssetLibrary::FindOrCreateTexture(DecodedImage& image) {
    if (const rhi::TextureHandle* found = m_textures.Find(image.key)) {
        return *found;
    }
    if (!image.image) {
        return tl::unexpected(std::move(image.image.error()));
    }
    core::Result<rhi::TextureHandle> texture = CreateTexture(m_renderer->Device(), *image.image, image.identity);
    if (!texture) {
        return texture;
    }
    m_textures.Insert(image.key, *texture);
    return texture;
}

core::Result<Model> AssetLibrary::BuildModel(DecodedModel& decoded, const std::string& key) {
    const resources::ModelData& data = decoded.data;
    std::vector<rhi::TextureHandle> textures;
    textures.reserve(decoded.images.size());
    for (DecodedImage& image : decoded.images) {
        core::Result<rhi::TextureHandle> texture = FindOrCreateTexture(image);
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

core::Result<const Model*> AssetLibrary::Build(std::string key, core::Result<DecodedModel> decoded) {
    if (const Model* found = m_models.Find(key)) {
        return found; // Loaded in the meantime by another request.
    }
    if (!decoded) {
        return tl::unexpected(std::move(decoded.error()));
    }
    core::Result<Model> model = BuildModel(*decoded, key);
    if (!model) {
        return tl::unexpected(std::move(model.error()));
    }
    DAI_LOG_INFO(kModule, "Loaded model '{}': {} meshes, {} materials, {} images", key, decoded->data.primitives.size(),
                 decoded->data.materials.size(), decoded->data.images.size());
    return &m_models.Insert(std::move(key), std::move(*model));
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
    core::Result<DecodedModel> decoded = Decode(m_root, *key);
    return Build(std::move(*key), std::move(decoded));
}

void AssetLibrary::LoadModelAsync(std::string_view path, core::WorkerPool& workers,
                                  std::function<void(core::Result<const Model*>)> done) {
    core::Result<std::string> key = resources::NormalizeAssetPath(path);
    if (!key) {
        done(tl::unexpected(std::move(key.error())));
        return;
    }
    if (const Model* found = m_models.Find(*key)) {
        done(found);
        return;
    }

    // The id is known once Submit returns. The job may finish before that, but its result waits for
    // Deliver, which runs on this thread.
    auto job = std::make_shared<core::JobId>(0);
    *job = workers.Submit([root = m_root, key = *key] { return Decode(root, key); },
                          [this, key = *key, job, done = std::move(done)](core::Result<DecodedModel> decoded) {
                              std::erase_if(m_pending, [&job](const PendingLoad& load) { return load.job == *job; });
                              done(Build(key, std::move(decoded)));
                          });
    m_pending.push_back({&workers, *job});
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
        if (!node.name.empty()) {
            world.Add<NodeName>(entity, node.name);
        }

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
