// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <cstdint>

namespace daidalos::texturetool {

/// What a flipbook shows (ADR-0053).
enum class FlipbookKind : std::uint8_t {
    /// Flames rising from a burner, looped.
    Fire,
    /// A column of smoke, looped, lit by its six-way maps.
    Smoke,
    /// A fireball that swells, burns out and leaves smoke, once over its frames.
    Explosion,
};

struct FlipbookSettings {
    FlipbookKind kind = FlipbookKind::Fire;
    std::uint32_t frames = 64;
    /// Pixels across a frame.
    std::uint32_t size = 128;
    /// Cells across the simulation's cube.
    std::uint32_t grid = 64;
    std::uint32_t seed = 1;
};

/// The images of a flipbook, each an atlas of columns by rows frames counted row by row, RGBA8 without mip
/// levels: the frames' color, unpremultiplied, in sRGB with coverage in alpha, and their six-way light
/// maps, linear: how much of a light from the right, top and front reaches the eye (positive, in RGB), and
/// from the left, bottom and back (negative), per unit of coverage.
struct Flipbook {
    resources::ImageData color;
    resources::ImageData lightPositive;
    resources::ImageData lightNegative;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
};

/// Simulates density and temperature on a cube of cells, moved by buoyancy and curl noise, and renders each
/// frame by marching rays through it along -Z: emission from the temperature, a black body, and the light of
/// six directions along the axes, from transmittance summed along each axis. Looped kinds blend their last
/// frames into their first, so that the loop does not jump.
[[nodiscard]] core::Result<Flipbook> MakeFlipbook(const FlipbookSettings& settings);

} // namespace daidalos::texturetool
