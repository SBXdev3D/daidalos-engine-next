// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/vec3.hpp>

namespace daidalos::renderer {

/// Physical light units (ADR-0043): point and spot lights in candela, directional lights in lux, emission
/// and the sky in nits (candela per square meter), exposure in EV100.

/// The linear sRGB color of a black body at this temperature, in kelvin, scaled to a luminance of 1: it
/// changes a light's hue, not its brightness. 6504 K is about white; lower is warmer, higher bluer. Clamped
/// to 1000 to 40000 K.
[[nodiscard]] glm::vec3 ColorTemperature(float kelvin);

/// The intensity of a point light that sends this many lumens in every direction.
[[nodiscard]] float PointCandela(float lumens);

/// The intensity of a spot light that sends this many lumens into its cone, of this outer angle from its
/// axis, in radians.
[[nodiscard]] float SpotCandela(float lumens, float outerAngle);

/// A range for a light of this intensity, in candela, that has none: where its illuminance falls to
/// 0.05 lux, between 1 and 64 meters.
[[nodiscard]] float LightRange(float candela);

/// What a luminance in nits is multiplied by before tonemapping, for a camera at this exposure value at
/// ISO 100: 1 / (1.2 * 2^ev100), the saturation-based sensitivity of photography. Higher values are darker.
[[nodiscard]] float ExposureScale(float ev100);

} // namespace daidalos::renderer
