// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace daidalos::resources {

/// Encodes interleaved float samples, from -1 to 1, to a 16-bit PCM WAV file (ADR-0123), the sound of a
/// recording. Samples beyond the range are clipped.
[[nodiscard]] core::Result<std::vector<std::byte>> EncodeWav(std::uint32_t sampleRate, std::uint32_t channels,
                                                             std::span<const float> samples);

} // namespace daidalos::resources
