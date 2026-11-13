// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/LightUnits.hpp>

#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace daidalos::renderer {

glm::vec3 ColorTemperature(float kelvin) {
    // The chromaticity of the Planckian locus, from Kim et al.'s cubic fits (2002), in CIE 1931 xy.
    const double t = std::clamp(static_cast<double>(kelvin), 1000.0, 40000.0);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double x = t <= 4000.0 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                                 : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    const double x2 = x * x;
    const double x3 = x2 * x;
    double y = 0.0;
    if (t <= 2222.0) {
        y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    } else if (t <= 4000.0) {
        y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    } else {
        y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    }
    // XYZ at a luminance of 1, then linear sRGB (D65).
    const double X = x / y;
    const double Z = (1.0 - x - y) / y;
    const glm::dvec3 rgb(3.2404542 * X - 1.5371385 - 0.4985314 * Z, -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
                         0.0556434 * X - 0.2040259 + 1.0572252 * Z);
    // Very warm colors fall outside sRGB: a negative blue is clipped, and the luminance restored.
    const glm::dvec3 clipped = glm::max(rgb, glm::dvec3(0.0));
    const double luminance = 0.2126 * clipped.r + 0.7152 * clipped.g + 0.0722 * clipped.b;
    return glm::vec3(clipped / luminance);
}

float PointCandela(float lumens) {
    return lumens / (4.0f * glm::pi<float>());
}

float SpotCandela(float lumens, float outerAngle) {
    // The solid angle of a cone: 2 pi (1 - cos angle).
    const float solidAngle =
        2.0f * glm::pi<float>() * (1.0f - std::cos(std::clamp(outerAngle, 0.001f, glm::pi<float>())));
    return lumens / solidAngle;
}

float LightRange(float candela) {
    constexpr float kLeastIlluminance = 0.05f;
    return std::clamp(std::sqrt(std::max(candela, 0.0f) / kLeastIlluminance), 1.0f, 64.0f);
}

float ExposureScale(float ev100) {
    return 1.0f / (1.2f * std::exp2(ev100));
}

} // namespace daidalos::renderer
