// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace daidalos::rhi {

/// Shaders are embedded as bytes (dai_add_shaders); Vulkan needs aligned 32-bit words.
template <std::size_t Size>
[[nodiscard]] std::vector<std::uint32_t> ToSpirv(const unsigned char (&bytes)[Size]) {
    static_assert(Size % sizeof(std::uint32_t) == 0);
    std::vector<std::uint32_t> words(Size / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes, Size);
    return words;
}

} // namespace daidalos::rhi
