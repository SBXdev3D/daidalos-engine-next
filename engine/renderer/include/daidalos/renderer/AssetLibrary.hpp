// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/renderer/Components.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/resources/Cache.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::renderer {

/// A model on the GPU, owned by an AssetLibrary.
struct Model {
    struct Part {
        MeshHandle mesh;
        MaterialHandle material;
    };
    struct Node {
        std::string name;
        std::optional<std::uint32_t> parent;
        math::Transform local;
        std::vector<Part> parts;
    };
    /// Parents come before their children.
    std::vector<Node> nodes;
};

/// Loads assets to the GPU, once each, and owns what it loaded (ADR-0016). Everything is released
/// together when the library is destroyed: nothing is counted and nothing is evicted, so a scene
/// keeps a library for as long as it needs its assets. Entities drawing destroyed assets are
/// harmless: their draws are skipped, or use the default material.
///
/// Models and textures are found under the root, by the identity of NormalizeAssetPath. A texture
/// shared by several models is loaded once. The renderer must outlive the library.
class AssetLibrary {
public:
    AssetLibrary(Renderer& renderer, std::filesystem::path root);
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;
    ~AssetLibrary();

    /// Returns the model, loading it the first time. The pointer lives as long as the library.
    /// A missing or invalid model or texture is an error that names the file, and the next request
    /// tries again.
    [[nodiscard]] core::Result<const Model*> LoadModel(std::string_view path);

    [[nodiscard]] std::size_t ModelCount() const { return m_models.Size(); }
    [[nodiscard]] std::size_t TextureCount() const { return m_textures.Size(); }

private:
    [[nodiscard]] core::Result<rhi::TextureHandle> LoadTexture(const resources::ImageSource& source,
                                                               const std::string& modelKey, std::uint32_t imageIndex);
    [[nodiscard]] core::Result<Model> BuildModel(const resources::ModelData& data, const std::string& key);

    Renderer* m_renderer;
    std::filesystem::path m_root;
    resources::Cache<Model> m_models;
    resources::Cache<rhi::TextureHandle> m_textures;
    std::vector<MeshHandle> m_meshes;
    std::vector<MaterialHandle> m_materials;
};

/// Creates an entity for each node of a model, under one new root entity at the origin, and returns
/// the root. A node with one part draws it, and further parts go to child entities.
[[nodiscard]] core::Result<ecs::Entity> Instantiate(ecs::World& world, const Model& model);

} // namespace daidalos::renderer
