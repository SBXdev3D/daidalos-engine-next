// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/WorkerPool.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/renderer/Components.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/resources/Cache.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
        /// The triangles, also kept on the CPU for collisions and queries.
        std::vector<glm::vec3> positions;
        std::vector<std::uint32_t> indices;
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
/// shared by several models is loaded once. The renderer must outlive the library, and so must the
/// worker pools it was given.
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

    /// Same as LoadModel, but files are read and decoded on a worker, so that the main thread keeps
    /// running. The function is called on the main thread, from WorkerPool::Deliver, or at once if the
    /// model is already there or the path is invalid. It is not called once the library is destroyed.
    /// Creating the GPU objects still happens on the main thread, in that Deliver.
    void LoadModelAsync(std::string_view path, core::WorkerPool& workers,
                        std::function<void(core::Result<const Model*>)> done);

    [[nodiscard]] std::size_t ModelCount() const { return m_models.Size(); }
    [[nodiscard]] std::size_t TextureCount() const { return m_textures.Size(); }

private:
    struct DecodedImage {
        /// The identity of the file, and the key of the texture in the cache.
        std::string identity;
        std::string key;
        core::Result<resources::ImageData> image;
    };
    /// A model read from disk, with nothing on the GPU yet.
    struct DecodedModel {
        resources::ModelData data;
        std::vector<DecodedImage> images;
    };
    struct PendingLoad {
        core::WorkerPool* workers = nullptr;
        core::JobId job = 0;
    };

    /// Touches nothing of the library, because it runs on workers.
    [[nodiscard]] static core::Result<DecodedModel> Decode(const std::filesystem::path& root, const std::string& key);
    [[nodiscard]] core::Result<const Model*> Build(std::string key, core::Result<DecodedModel> decoded);
    [[nodiscard]] core::Result<rhi::TextureHandle> FindOrCreateTexture(DecodedImage& image);
    [[nodiscard]] core::Result<Model> BuildModel(DecodedModel& decoded, const std::string& key);

    Renderer* m_renderer;
    std::filesystem::path m_root;
    resources::Cache<Model> m_models;
    resources::Cache<rhi::TextureHandle> m_textures;
    std::vector<MeshHandle> m_meshes;
    std::vector<MaterialHandle> m_materials;
    std::vector<PendingLoad> m_pending;
};

/// Creates an entity for each node of a model, under one new root entity at the origin, and returns
/// the root. A node with one part draws it, and further parts go to child entities.
[[nodiscard]] core::Result<ecs::Entity> Instantiate(ecs::World& world, const Model& model);

} // namespace daidalos::renderer
