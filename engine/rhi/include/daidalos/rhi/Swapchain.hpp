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

struct SwapchainConfig {
    /// Size in pixels. A zero size is allowed (minimized window): no frame is drawn until it changes.
    Extent extent;
    bool vsync = true;
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

    [[nodiscard]] Format ColorFormat() const;
    [[nodiscard]] Extent CurrentExtent() const;

private:
    friend class Device;
    struct Impl;

    explicit Swapchain(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::rhi
