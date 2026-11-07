// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/resources/ImageData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <span>
#include <string>

namespace daidalos::renderer {

/// A metallic-roughness material on the GPU. Textures are optional and must outlive the material.
struct MaterialDesc {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    /// Pixels with a lower alpha are discarded. 0 keeps every pixel.
    float alphaCutoff = 0.0f;
    /// Draws both sides of the triangles.
    bool doubleSided = false;
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
