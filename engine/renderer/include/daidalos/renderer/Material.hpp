// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/resources/ImageData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <limits>
#include <span>
#include <string>

namespace daidalos::renderer {

/// A metallic-roughness material on the GPU. Textures are optional and must outlive the material.
struct MaterialDesc {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    /// The emission is a light (ADR-0044): its triangles light the scene, as lamps do. Otherwise it is only
    /// seen, by the camera and by reflections.
    bool emissiveLight = true;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    /// Pixels with a lower alpha are discarded. 0 keeps every pixel.
    float alphaCutoff = 0.0f;
    /// Draws both sides of the triangles.
    bool doubleSided = false;
    /// Glass (ADR-0042): the fraction of the light that goes through, tinted by the base color. Above zero,
    /// the material is drawn by the glass pass, after everything opaque, and casts no shadow.
    float transmission = 0.0f;
    /// The index of refraction: how much light bends, and how much the glass reflects.
    float ior = 1.5f;
    /// Thin glass, such as a window, bends nothing. Thick glass is a closed mesh: light bends where it enters
    /// and where it leaves, and is absorbed on the way.
    bool thin = true;
    /// Thick glass: the color white light takes after attenuationDistance meters inside it.
    glm::vec3 attenuationColor{1.0f};
    float attenuationDistance = std::numeric_limits<float>::infinity();
    /// A clear varnish over the material (KHR_materials_clearcoat): how much of it, from 0 to 1, and its own
    /// roughness. Its reflection sits over the base, which it dims by what it reflects (ADR-0056).
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    /// The soft rim of light of cloth, felt and dust (KHR_materials_sheen): its color, black for none, and its
    /// roughness.
    glm::vec3 sheenColor{0.0f};
    float sheenRoughness = 0.0f;
    /// How much water the surface soaks up (ADR-0061): wet, a porous surface (concrete, cloth, wood) darkens
    /// much, a sealed one (metal, glazed tile) little. From the material's glTF extras {"porosity": 0.7}.
    float porosity = 0.5f;
    rhi::TextureHandle baseColorTexture;
    rhi::TextureHandle metallicRoughnessTexture;
    rhi::TextureHandle normalTexture;
    rhi::TextureHandle occlusionTexture;
    rhi::TextureHandle emissiveTexture;
};

/// Creates a sampled texture from an image and uploads it. Not allowed between
/// rhi::Device::BeginFrame and EndFrame.
[[nodiscard]] core::Result<rhi::TextureHandle> CreateTexture(rhi::Device& device, const resources::ImageData& image,
                                                             const std::string& name);

/// Creates a sampled cube texture from six square images of the same size and format, in the order +X, -X,
/// +Y, -Y, +Z, -Z (rhi::TextureDesc::cube), and uploads it. Fails with InvalidArgument if they differ.
[[nodiscard]] core::Result<rhi::TextureHandle>
CreateCubeTexture(rhi::Device& device, std::span<const resources::ImageData, 6> faces, const std::string& name);

} // namespace daidalos::renderer
