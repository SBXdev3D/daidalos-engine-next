// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace daidalos::resources {

/// Encodes an RGBA8 image, rows from the top, to PNG (ADR-0077). Alpha is written as it is.
[[nodiscard]] core::Result<std::vector<std::byte>> EncodePng(std::uint32_t width, std::uint32_t height,
                                                             std::span<const std::uint8_t> rgba);

/// Encodes an image of linear RGB floats, rows from the top, to OpenEXR (ADR-0077): one part, scanlines,
/// no compression, 32-bit float channels B, G and R, as every reader takes.
[[nodiscard]] core::Result<std::vector<std::byte>> EncodeExr(std::uint32_t width, std::uint32_t height,
                                                             std::span<const float> rgb);

/// Writes bytes to a file, making its directory. Fails with IoError.
[[nodiscard]] core::Result<void> WriteFile(const std::filesystem::path& path, std::span<const std::byte> bytes);

} // namespace daidalos::resources
