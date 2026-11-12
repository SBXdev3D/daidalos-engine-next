// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/resources/AssetPath.hpp>
#include <daidalos/scene/AssetLibrary.hpp>

#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <utility>

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";

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

AssetLibrary::AssetLibrary(renderer::Renderer& renderer, std::filesystem::path root)
    : m_renderer(&renderer), m_root(std::move(root)) {}

AssetLibrary::~AssetLibrary() {
    for (const PendingLoad& load : m_pending) {
        load.workers->Cancel(load.job);
    }
    // Effects and materials first: they refer to the textures.
    for (const renderer::ParticleEffectHandle effect : m_particleEffects) {
        m_renderer->DestroyParticleEffect(effect);
    }
    for (const renderer::MaterialHandle material : m_materials) {
        m_renderer->DestroyMaterial(material);
    }
    for (const renderer::MeshHandle mesh : m_meshes) {
        m_renderer->DestroyMesh(mesh);
    }
    m_textures.ForEach([this](rhi::TextureHandle texture) { m_renderer->Device().DestroyTexture(texture); });
    m_fonts.ForEach([this](const ui::Font& font) { m_renderer->Device().DestroyTexture(font.atlas); });
}

core::Result<AssetLibrary::DecodedModel> AssetLibrary::Decode(const std::filesystem::path& root,
                                                              const std::string& key) {
    DAI_PROFILE_SCOPE("scene::DecodeModel");
    DAI_TRY_ASSIGN(std::filesystem::path file, resources::ResolveAssetPath(root, key));
    DAI_TRY_ASSIGN(resources::ModelData data, resources::LoadModelFile(file));

    DecodedModel decoded{std::move(data), {}};
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
        DAI_TRY_ASSIGN(std::string normalized,
                       resources::NormalizeAssetPath(resources::ToUtf8(source.file.lexically_relative(root))));
        DAI_TRY_ASSIGN(std::filesystem::path resolved, resources::ResolveAssetPath(root, normalized));
        // A file used in two ways is two textures: the use decides how it is decoded.
        std::string textureKey = std::format("{}|{}", normalized, ToString(source.use));
        decoded.images.push_back(
            {std::move(normalized), std::move(textureKey), resources::LoadImageFile(resolved, source.use)});
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
    core::Result<rhi::TextureHandle> texture =
        renderer::CreateTexture(m_renderer->Device(), *image.image, image.identity);
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
        DAI_TRY_ASSIGN(rhi::TextureHandle texture, FindOrCreateTexture(image));
        textures.push_back(texture);
    }
    const auto textureOf = [&textures](const std::optional<std::uint32_t>& image) {
        return image.has_value() ? textures[*image] : rhi::TextureHandle{};
    };

    // What is created on the way stays in the library, also if a later step fails.
    std::vector<renderer::MaterialHandle> materials;
    materials.reserve(data.materials.size());
    for (const resources::MaterialData& material : data.materials) {
        renderer::MaterialDesc desc;
        desc.baseColorFactor = material.baseColorFactor;
        desc.emissiveFactor = material.emissiveFactor;
        desc.metallicFactor = material.metallicFactor;
        desc.roughnessFactor = material.roughnessFactor;
        desc.normalScale = material.normalScale;
        desc.occlusionStrength = material.occlusionStrength;
        desc.alphaCutoff = material.alphaCutoff;
        desc.doubleSided = material.doubleSided;
        desc.transmission = material.transmission;
        desc.ior = material.ior;
        desc.thin = material.thickness <= 0.0f;
        desc.attenuationColor = material.attenuationColor;
        desc.attenuationDistance = material.attenuationDistance;
        desc.baseColorTexture = textureOf(material.baseColorImage);
        desc.metallicRoughnessTexture = textureOf(material.metallicRoughnessImage);
        desc.normalTexture = textureOf(material.normalImage);
        desc.occlusionTexture = textureOf(material.occlusionImage);
        desc.emissiveTexture = textureOf(material.emissiveImage);
        DAI_TRY_ASSIGN(renderer::MaterialHandle created, m_renderer->CreateMaterial(desc));
        m_materials.push_back(created);
        materials.push_back(created);
    }

    Model model;
    std::vector<Model::Part>& parts = model.parts;
    parts.reserve(data.primitives.size());
    for (std::size_t index = 0; index < data.primitives.size(); ++index) {
        const resources::ModelPrimitive& primitive = data.primitives[index];
        DAI_TRY_ASSIGN(renderer::MeshHandle mesh,
                       m_renderer->CreateMesh(primitive.mesh, std::format("{} #{}", key, index)));
        m_meshes.push_back(mesh);
        Model::Part& part = parts.emplace_back();
        part.mesh = mesh;
        part.material = primitive.material.has_value() ? materials[*primitive.material] : renderer::MaterialHandle{};
        part.positions.reserve(primitive.mesh.vertices.size());
        for (const resources::Vertex& vertex : primitive.mesh.vertices) {
            part.positions.push_back(vertex.position);
        }
        part.indices = primitive.mesh.indices;
    }

    model.nodes.reserve(data.nodes.size());
    std::vector<math::Transform> restPose;
    restPose.reserve(data.nodes.size());
    for (const resources::ModelNode& node : data.nodes) {
        Model::Node& added = model.nodes.emplace_back();
        added.name = node.name;
        added.parent = node.parent;
        added.local = node.local;
        added.skin = node.skin;
        added.parts = node.primitives;
        restPose.push_back(node.local);
    }
    model.skins.reserve(data.skins.size());
    for (const resources::SkinData& skin : data.skins) {
        model.skins.push_back({skin.joints, std::make_shared<const std::vector<glm::mat4>>(skin.inverseBindMatrices)});
    }
    if (!data.animations.empty()) {
        model.clips =
            std::make_shared<const animation::ClipSet>(animation::MakeClipSet(data.animations, std::move(restPose)));
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
    const auto start = std::chrono::steady_clock::now();
    DAI_TRY_ASSIGN(Model model, BuildModel(*decoded, key));
    // The time spent on the main thread, creating GPU objects: what a load costs the frame loop.
    const std::chrono::duration<double, std::milli> built = std::chrono::steady_clock::now() - start;
    DAI_LOG_INFO(kModule,
                 "Loaded model '{}': {} meshes, {} materials, {} images, {} skins, {} animations, built in {:.1f} ms",
                 key, decoded->data.primitives.size(), decoded->data.materials.size(), decoded->data.images.size(),
                 decoded->data.skins.size(), decoded->data.animations.size(), built.count());
    return &m_models.Insert(std::move(key), std::move(model));
}

core::Result<renderer::ParticleEffectHandle>
AssetLibrary::CreateParticleEffect(const renderer::ParticleEffectDesc& desc) {
    DAI_TRY_ASSIGN(renderer::ParticleEffectHandle effect, m_renderer->CreateParticleEffect(desc));
    m_particleEffects.push_back(effect);
    return effect;
}

core::Result<rhi::TextureHandle> AssetLibrary::LoadTexture(std::string_view path, resources::TextureUse use) {
    DAI_TRY_ASSIGN(std::string identity, resources::NormalizeAssetPath(path));
    DAI_TRY_ASSIGN(std::filesystem::path file, resources::ResolveAssetPath(m_root, identity));
    std::string key = std::format("{}|{}", identity, ToString(use));
    DecodedImage image{identity, std::move(key), resources::LoadImageFile(file, use)};
    return FindOrCreateTexture(image);
}

core::Result<rhi::TextureHandle> AssetLibrary::LoadCubeTexture(std::span<const std::string, 6> faces) {
    std::array<std::string, 6> identities;
    std::string key = "cube";
    for (std::size_t face = 0; face < faces.size(); ++face) {
        DAI_TRY_ASSIGN(identities[face], resources::NormalizeAssetPath(faces[face]));
        key += "|" + identities[face];
    }
    if (const rhi::TextureHandle* found = m_textures.Find(key)) {
        return *found;
    }
    std::array<resources::ImageData, 6> images;
    for (std::size_t face = 0; face < faces.size(); ++face) {
        DAI_TRY_ASSIGN(std::filesystem::path file, resources::ResolveAssetPath(m_root, identities[face]));
        DAI_TRY_ASSIGN(images[face], resources::LoadImageFile(file, resources::TextureUse::Color));
    }
    DAI_TRY_ASSIGN(rhi::TextureHandle texture,
                   renderer::CreateCubeTexture(m_renderer->Device(), images, identities.front()));
    m_textures.Insert(key, texture);
    return texture;
}

std::vector<AssetLibrary::AssetInfo> AssetLibrary::Describe() const {
    std::vector<AssetInfo> assets;
    m_models.ForEachEntry(
        [&assets](const std::string& key, const Model& /*model*/) { assets.push_back({AssetInfo::Kind::Model, key}); });
    m_textures.ForEachEntry([&assets](const std::string& key, const rhi::TextureHandle& /*texture*/) {
        assets.push_back({AssetInfo::Kind::Texture, key});
    });
    m_fonts.ForEachEntry([&assets](const std::string& key, const ui::Font& /*font*/) {
        assets.push_back({AssetInfo::Kind::Font, key});
    });
    return assets;
}

core::Result<const ui::Font*> AssetLibrary::LoadFont(std::string_view path) {
    DAI_PROFILE_SCOPE("scene::LoadFont");
    DAI_TRY_ASSIGN(std::string key, resources::NormalizeAssetPath(path));
    if (const ui::Font* found = m_fonts.Find(key)) {
        return found;
    }
    DAI_TRY_ASSIGN(std::filesystem::path file, resources::ResolveAssetPath(m_root, key));
    const auto start = std::chrono::steady_clock::now();
    DAI_TRY_ASSIGN(resources::FontData data, resources::LoadFontFile(file));
    DAI_TRY_ASSIGN(rhi::TextureHandle atlas, ui::CreateFontAtlas(m_renderer->Device(), data, key));
    DAI_LOG_INFO(
        kModule, "Font '{}' baked in {} ms: {} glyphs, {}x{} atlas", key,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
        data.glyphs.size(), data.atlasWidth, data.atlasHeight);
    return &m_fonts.Insert(std::move(key), ui::Font{std::move(data), atlas});
}

core::Result<const Model*> AssetLibrary::LoadModel(std::string_view path) {
    DAI_PROFILE_SCOPE("scene::LoadModel");
    DAI_TRY_ASSIGN(std::string key, resources::NormalizeAssetPath(path));
    if (const Model* found = m_models.Find(key)) {
        return found;
    }
    core::Result<DecodedModel> decoded = Decode(m_root, key);
    return Build(std::move(key), std::move(decoded));
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
    // Joints may come after the meshes that follow them, so those are given their joints at the end.
    std::vector<std::pair<ecs::Entity, std::uint32_t>> skinned;
    for (const Model::Node& node : model.nodes) {
        const ecs::Entity entity = world.Create();
        ecs::LocalTransform transform;
        static_cast<math::Transform&>(transform) = node.local;
        world.Add<ecs::LocalTransform>(entity, transform);
        const ecs::Entity parent = node.parent.has_value() ? entities[*node.parent] : root;
        DAI_TRY(world.SetParent(entity, parent));
        entities.push_back(entity);
        if (!node.name.empty()) {
            world.Add<NodeName>(entity, node.name);
        }

        for (std::size_t index = 0; index < node.parts.size(); ++index) {
            const Model::Part& part = model.parts[node.parts[index]];
            ecs::Entity holder = entity;
            if (index > 0) {
                holder = world.Create();
                world.Add<ecs::LocalTransform>(holder);
                DAI_TRY(world.SetParent(holder, entity));
            }
            if (node.skin.has_value() && *node.skin < model.skins.size()) {
                world.Add<SkinnedMeshRenderer>(holder, part.mesh, part.material, std::vector<ecs::Entity>{},
                                               model.skins[*node.skin].inverseBindMatrices);
                skinned.emplace_back(holder, *node.skin);
            } else {
                world.Add<MeshRenderer>(holder, part.mesh, part.material);
            }
        }
    }

    for (const auto& [entity, skin] : skinned) {
        std::vector<ecs::Entity>& joints = world.Get<SkinnedMeshRenderer>(entity).joints;
        for (const std::uint32_t joint : model.skins[skin].joints) {
            joints.push_back(entities[joint]);
        }
    }
    if (model.clips != nullptr) {
        world.Add<animation::Animator>(root, model.clips, entities);
    }
    return root;
}

} // namespace daidalos::scene
