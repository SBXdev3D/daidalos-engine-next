// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace daidalos::resources {

/// Layout of mesh vertices.
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    /// Direction of increasing u, with the handedness of the bitangent in w. All zeros if the asset has
    /// no tangents: they are generated with normal mapping in Phase 6.
    glm::vec4 tangent{0.0f};
    glm::vec2 uv{0.0f};
};

/// Triangles, with counter-clockwise front faces.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// Where the pixels of an image are: in a file of their own, or embedded in the model.
struct ImageSource {
    /// Empty for an embedded image.
    std::filesystem::path file;
    std::vector<std::byte> embedded;
    TextureUse use = TextureUse::Color;
    std::string name;
};

/// The glTF metallic-roughness material. Images are indices into ModelData::images.
struct MaterialData {
    std::string name;
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    /// Pixels with a lower alpha are discarded. 0 keeps every pixel.
    float alphaCutoff = 0.0f;
    bool doubleSided = false;
    std::optional<std::uint32_t> baseColorImage;
    std::optional<std::uint32_t> metallicRoughnessImage;
    std::optional<std::uint32_t> normalImage;
    std::optional<std::uint32_t> occlusionImage;
    std::optional<std::uint32_t> emissiveImage;
};

/// One drawable part: a mesh and its material.
struct ModelPrimitive {
    MeshData mesh;
    /// Index into ModelData::materials. Without one, the default material applies.
    std::optional<std::uint32_t> material;
};

/// A node of the scene. Parents come before their children.
struct ModelNode {
    std::string name;
    /// Index into ModelData::nodes. Without one, the node is a root.
    std::optional<std::uint32_t> parent;
    math::Transform local;
    /// Indices into ModelData::primitives.
    std::vector<std::uint32_t> primitives;
};

/// The content of a glTF file, independent of the GPU API. Images are not decoded: the caller can
/// recognize the ones it already has.
struct ModelData {
    std::vector<ModelPrimitive> primitives;
    std::vector<MaterialData> materials;
    std::vector<ImageSource> images;
    std::vector<ModelNode> nodes;
};

/// Loads the default scene of a .gltf or .glb file, and the buffers that it refers to. Fails with
/// NotFound on a missing file, ParseError on invalid content. Non-triangle primitives are skipped with
/// a warning, and normals missing from a primitive are computed from its triangles.
[[nodiscard]] core::Result<ModelData> LoadModelFile(const std::filesystem::path& path);

} // namespace daidalos::resources
