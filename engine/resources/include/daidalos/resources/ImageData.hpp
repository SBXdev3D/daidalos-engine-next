// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace daidalos::resources {

/// What the values of a texture mean, which decides how it is decoded.
enum class TextureUse {
    /// Colors in sRGB: base color and emissive maps.
    Color,
    /// Linear values: metallic-roughness and occlusion maps.
    Data,
    /// Tangent-space normals, in the red and green channels.
    Normal,
};

enum class PixelFormat { Rgba8Unorm, Rgba8Srgb, Bc5Unorm, Bc7Unorm, Bc7Srgb };

/// A decoded image, independent of the GPU API.
struct ImageData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    PixelFormat format = PixelFormat::Rgba8Unorm;
    /// The mip levels from the largest, tightly packed.
    std::vector<std::byte> pixels;
};

/// Decodes a PNG, JPEG or KTX2 file held in memory. The name appears in error messages.
/// PNG and JPEG, the development fallback, are decoded to RGBA8 with a full mip chain. KTX2 files in
/// UASTC or ETC1S are transcoded to BC7 (BC5 for normal maps), and other KTX2 files must already be in
/// a PixelFormat. Fails with ParseError on invalid data and NotSupported on other formats.
[[nodiscard]] core::Result<ImageData> DecodeImage(std::span<const std::byte> bytes, TextureUse use,
                                                  const std::string& name);

/// Reads and decodes an image file. A missing file is NotFound.
[[nodiscard]] core::Result<ImageData> LoadImageFile(const std::filesystem::path& path, TextureUse use);

/// Adds the mip levels of a single-level RGBA8 image, by averaging 2x2 pixels. Colors of Rgba8Srgb
/// images are averaged in linear space, and alpha always is.
[[nodiscard]] ImageData GenerateMips(ImageData image);

} // namespace daidalos::resources
