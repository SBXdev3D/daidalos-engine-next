// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Types.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace daidalos::rhi {

class Device;

/// How the values written to a swapchain's images are shown (ADR-0048).
enum class ColorSpace : std::uint8_t {
    /// sRGB primaries; the format encodes the linear values written.
    Srgb,
    /// HDR10: Rec. 2020 primaries, values encoded with the ST 2084 (PQ) curve by the shader, 10 bits each.
    Hdr10,
};

struct SwapchainConfig {
    /// Size in pixels. A zero size is allowed (minimized window): no frame is drawn until it changes.
    Extent extent;
    bool vsync = true;
    /// HDR10 when the surface offers it; sRGB otherwise.
    bool hdr = false;
    /// In HDR10, the brightest the content gets, in nits, sent to the display when the device supports
    /// VK_EXT_hdr_metadata. 0 sends nothing.
    float maxLuminance = 0.0f;

    bool operator==(const SwapchainConfig&) const = default;
};

/// Creates a VkSurfaceKHR from a VkInstance, both carried as integers (platform::Window provides it).
using SurfaceFactory = std::function<core::Result<std::uint64_t>(std::uintptr_t instance)>;

/// The images presented to a window. It must be destroyed before its Device.
class Swapchain {
public:
    /// Fails with NotSupported if the device cannot present to the surface or has no 8-bit sRGB format.
    static core::Result<Swapchain> Create(Device& device, const SurfaceFactory& createSurface,
                                          const SwapchainConfig& config);

    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    /// Asks for another size or presentation mode. The swapchain is recreated by the next BeginFrame.
    void Configure(const SwapchainConfig& config);

    /// Both may change when the swapchain is recreated, by BeginFrame.
    [[nodiscard]] Format ColorFormat() const;
    [[nodiscard]] ColorSpace CurrentColorSpace() const;
    [[nodiscard]] Extent CurrentExtent() const;

private:
    friend class Device;
    struct Impl;

    explicit Swapchain(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::rhi
