// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

namespace daidalos::resources {

/// How a real lamp sends its light around its axis, from an IES file (ANSI/IESNA LM-63): its intensity by
/// the angle from the axis, from 0 (straight along it) to 180 degrees, averaged around it (ADR-0057).
struct LightProfile {
    /// Evenly spaced from 0 to 180 degrees, kSamples of them, the brightest 1.
    std::vector<float> samples;
    /// Candela in the brightest direction, as the file gives them, times its multiplier.
    float peakCandela = 0.0f;

    static constexpr std::size_t kSamples = 128;
};

/// Reads the text of an IES file of type C (vertical angles from the axis). A profile that varies around the
/// axis is averaged around it. Fails with ParseError on a file it cannot read.
[[nodiscard]] core::Result<LightProfile> ParseIes(std::string_view text);
[[nodiscard]] core::Result<LightProfile> LoadIesFile(const std::filesystem::path& path);

} // namespace daidalos::resources
