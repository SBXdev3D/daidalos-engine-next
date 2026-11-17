// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace daidalos::resources {

/// Layout of mesh vertices.
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    /// Direction of increasing u, with the handedness of the bitangent in w, as in glTF: the bitangent
    /// is cross(normal, tangent) * w. All zeros when the mesh has no normal map and no tangents.
    glm::vec4 tangent{0.0f};
    glm::vec2 uv{0.0f};
};

/// The joints that move a vertex, as indices into the joints of the skin, and how much each one
/// counts. The weights add up to 1.
struct SkinWeights {
    std::array<std::uint16_t, 4> joints{};
    glm::vec4 weights{1.0f, 0.0f, 0.0f, 0.0f};
};

/// Triangles, with counter-clockwise front faces.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    /// Empty for a rigid mesh, one per vertex for a skinned one.
    std::vector<SkinWeights> skin;
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
    /// Nits, times the emissive texture, with KHR_materials_emissive_strength applied (ADR-0043).
    glm::vec3 emissiveFactor{0.0f};
    /// The emission lights the scene (ADR-0044). The material's glTF extras {"light": false} make it only
    /// seen.
    bool emissiveLight = true;
    /// What the surface is made of, from the material's glTF extras {"surface": "metal"}: what impacts throw
    /// up and how they sound (ADR-0053). Empty when the file does not say.
    std::string surface;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    /// Pixels with a lower alpha are discarded. 0 keeps every pixel.
    float alphaCutoff = 0.0f;
    bool doubleSided = false;
    /// KHR_materials_transmission: the fraction of the light that goes through. Above zero, the material is
    /// glass (ADR-0042).
    float transmission = 0.0f;
    /// KHR_materials_ior: the index of refraction.
    float ior = 1.5f;
    /// How much water it soaks up, from the extras {"porosity": 0.8} or from its surface (ADR-0061).
    float porosity = 0.5f;
    /// Meters of relief across the height in the metallic-roughness image's alpha, from the extras
    /// {"height": 0.012} (ADR-0064); 0 for none.
    float heightScale = 0.0f;
    /// KHR_materials_clearcoat: a clear varnish over the material, how much of it and how rough.
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    /// KHR_materials_sheen: the soft rim of light on cloth and dust, its color and roughness.
    glm::vec3 sheenColor{0.0f};
    float sheenRoughness = 0.0f;
    /// KHR_materials_volume: meters through the glass, in the mesh's space. Zero is thin glass, which bends
    /// nothing; otherwise the mesh is a closed volume, and light bends where it enters and leaves it.
    float thickness = 0.0f;
    /// KHR_materials_volume: the color white light takes after attenuationDistance meters in the glass.
    glm::vec3 attenuationColor{1.0f};
    float attenuationDistance = std::numeric_limits<float>::infinity();
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

/// The joints that a skinned mesh follows.
struct SkinData {
    std::string name;
    /// Indices into ModelData::nodes.
    std::vector<std::uint32_t> joints;
    /// For each joint, from the space of the mesh to the space of the joint in the bind pose.
    std::vector<glm::mat4> inverseBindMatrices;
};

enum class LightKind : std::uint8_t { Point, Spot, Directional };

/// A light of KHR_lights_punctual, in its units (ADR-0043). It shines down its node's local -Z.
struct LightSource {
    std::string name;
    LightKind kind = LightKind::Point;
    /// Linear color.
    glm::vec3 color{1.0f};
    /// Candela for point and spot lights, lux for directional lights.
    float intensity = 1.0f;
    /// Meters; none is infinite.
    std::optional<float> range;
    /// Spot lights: radians from the axis, full light inside the inner angle, none outside the outer.
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.785398f;
};

/// A node of the scene. Parents come before their children.
struct ModelNode {
    std::string name;
    /// Index into ModelData::nodes. Without one, the node is a root.
    std::optional<std::uint32_t> parent;
    math::Transform local;
    /// Indices into ModelData::primitives.
    std::vector<std::uint32_t> primitives;
    /// Index into ModelData::skins, for a node whose primitives are skinned. Such a node's own
    /// transform does not move them: only the joints do.
    std::optional<std::uint32_t> skin;
    /// Index into ModelData::lights.
    std::optional<std::uint32_t> light;
};

enum class AnimationPath : std::uint8_t { Translation, Rotation, Scale };
enum class AnimationInterpolation : std::uint8_t { Step, Linear };

/// Keys of one property of one node. A rotation is a quaternion stored as (x, y, z, w); a translation
/// or a scale leaves w at 0.
struct AnimationChannel {
    /// Index into ModelData::nodes.
    std::uint32_t node = 0;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    /// Seconds, increasing.
    std::vector<float> times;
    std::vector<glm::vec4> values;
};

/// A glTF animation: one clip, such as walking.
struct AnimationData {
    std::string name;
    std::vector<AnimationChannel> channels;
};

/// The content of a glTF file, independent of the GPU API. Images are not decoded: the caller can
/// recognize the ones it already has.
struct ModelData {
    std::vector<ModelPrimitive> primitives;
    std::vector<MaterialData> materials;
    std::vector<ImageSource> images;
    std::vector<ModelNode> nodes;
    std::vector<SkinData> skins;
    std::vector<AnimationData> animations;
    std::vector<LightSource> lights;
};

/// Loads the default scene of a .gltf or .glb file, and the buffers that it refers to. Fails with
/// NotFound on a missing file, ParseError on invalid content. Non-triangle primitives are skipped with
/// a warning, and normals missing from a primitive are computed from its triangles. Skins and
/// animations come too: cubic spline keys are read as linear ones with a warning, and morph targets
/// are ignored.
[[nodiscard]] core::Result<ModelData> LoadModelFile(const std::filesystem::path& path);

/// The files a glTF model reads besides itself, its buffers and images, as its URIs give them: relative to the
/// model's directory. Embedded data is not listed. What an import copies with the model.
[[nodiscard]] core::Result<std::vector<std::filesystem::path>> ModelFiles(const std::filesystem::path& path);

/// Fills the tangents of a mesh with MikkTSpace, the standard of glTF and of the tools that bake normal
/// maps. Vertices whose triangles disagree, along UV seams, are split, so the mesh can gain vertices.
/// LoadModelFile does it for normal-mapped primitives that come without tangents.
void GenerateTangents(MeshData& mesh);

} // namespace daidalos::resources
