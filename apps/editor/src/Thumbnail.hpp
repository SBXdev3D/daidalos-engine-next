// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/resources/ImageData.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <cstdint>
#include <vector>

namespace editor {

/// The side of the asset browser's thumbnails, in pixels.
inline constexpr std::uint32_t kThumbnailSize = 128;

/// A model drawn as clay, from above and to the side, framed to fit, on a transparent background: size by size
/// RGBA8 pixels, sRGB-encoded, as the interface shows them. Drawn on the CPU from the model's triangles, so a
/// worker can draw it while the editor runs, without the renderer (ADR-0055). Each material tints the clay with
/// its base color and glows with its emission.
[[nodiscard]] std::vector<std::uint8_t> RenderClay(const daidalos::resources::ModelData& model, std::uint32_t size);

/// An image shrunk to size by size pixels, cropped to a square at its center, as sRGB-encoded RGBA8. Empty if
/// the image is not in an RGBA8 format.
[[nodiscard]] std::vector<std::uint8_t> Shrink(const daidalos::resources::ImageData& image, std::uint32_t size);

} // namespace editor
