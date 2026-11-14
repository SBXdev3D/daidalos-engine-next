// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// Bloom without threshold (ADR-0048): the image blurred at six scales, from half its size to a 64th,
/// averaged. The output pass replaces a share of the image with it. The Device must outlive it.
class Bloom {
public:
    static constexpr std::uint32_t kLevels = 6;

    static core::Result<Bloom> Create(rhi::Device& device);

    Bloom(Bloom&& other) noexcept;
    Bloom& operator=(Bloom&& other) = delete;
    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;
    ~Bloom();

    /// Follows the size of the image.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Adds the passes that blur an HDR image, as "Bloom". Valid after Resize.
    void AddTo(RenderGraph& graph, rhi::TextureHandle image);

    /// The bloom, at half the image's size, as bright overall as the image.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_up[0]; }

private:
    explicit Bloom(rhi::Device& device);
    void DestroyLevels();

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    /// Each level downsampled from the one above, and blurred back up from the one below; the smallest
    /// level has no up image: its down image stands for it.
    std::array<rhi::TextureHandle, kLevels> m_down{};
    std::array<rhi::TextureHandle, kLevels - 1> m_up{};
    std::array<rhi::Extent, kLevels> m_extents{};
    rhi::Extent m_extent;
};

} // namespace daidalos::renderer
