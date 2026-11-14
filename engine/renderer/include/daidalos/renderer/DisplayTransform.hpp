// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace daidalos::renderer {

/// How the display transform (ADR-0048) brings exposed light into what a display shows. These are the
/// CPU twins of the output pass's functions (shaders/Output.slang), for tests and for screens that need
/// to know what a scene color will look like, such as the brightness calibration.

/// The curve that brings exposed light, unbounded, into the display's range.
enum class ToneMapper : std::uint8_t {
    /// Blender's and Filament's AgX: bright colors go towards white instead of clipping to saturated
    /// primaries. The default.
    AgX,
    /// The Khronos PBR Neutral tone map (ADR-0022): colors below 0.76 are kept as they are, for comparisons.
    Neutral,
};

/// Linear Rec. 709 light, exposed, to linear Rec. 709 from 0 to 1, with AgX's base look.
[[nodiscard]] glm::vec3 AgX(glm::vec3 color);

/// Linear Rec. 709 light, exposed, to linear Rec. 709 from 0 to 1, with Khronos PBR Neutral.
[[nodiscard]] glm::vec3 PbrNeutral(glm::vec3 color);

/// Spreads an SDR image, from 0 to 1, up to headroom times paper white for an HDR display: the largest
/// channel x becomes x / (1 - x (1 - 1 / headroom)), the inverse of a Reinhard curve, and the others follow
/// it so that hues stay. 1 becomes the headroom; dark colors barely change. A headroom of 1 changes nothing.
[[nodiscard]] glm::vec3 ExpandHighlights(glm::vec3 color, float headroom);

/// What a display shows for an exposed scene color, in linear Rec. 709 relative to paper white: from 0 to 1
/// on an SDR display, up to the headroom on an HDR one.
[[nodiscard]] glm::vec3 DisplayColor(glm::vec3 color, ToneMapper toneMapper, float headroom = 1.0f);

/// Linear Rec. 709 to linear Rec. 2020, the primaries of HDR10 (ITU-R BT.2087).
[[nodiscard]] glm::vec3 Rec709ToRec2020(glm::vec3 color);

/// The SMPTE ST 2084 (PQ) encoding of HDR10: a luminance from 0 to 10,000 nits to a signal from 0 to 1.
[[nodiscard]] float PqEncode(float nits);

} // namespace daidalos::renderer
