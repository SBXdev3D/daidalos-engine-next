// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// Screen-space ambient occlusion at half resolution, from the depth and normals of the pre-pass, then
/// smoothed without crossing edges (ADR-0024). The Device must outlive it.
class AmbientOcclusionPass {
public:
    static core::Result<AmbientOcclusionPass> Create(rhi::Device& device);

    AmbientOcclusionPass(AmbientOcclusionPass&& other) noexcept;
    AmbientOcclusionPass& operator=(AmbientOcclusionPass&& other) = delete;
    AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass& operator=(const AmbientOcclusionPass&) = delete;
    ~AmbientOcclusionPass();

    /// Follows the size of the frame: the occlusion is half of it.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent frame);

    /// Adds the passes that compute the occlusion of the frame from the pre-pass's depth and normals, in
    /// the group "Occlusion"; frameBuffer is the storage index of the frame's FrameData. Passes that read
    /// Output() must come after them.
    void AddTo(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle depth, rhi::TextureHandle normals);

    /// 1 where the ambient light reaches, down to 0 where geometry closes it. Valid after Resize.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_blurred; }

private:
    explicit AmbientOcclusionPass(rhi::Device& device) : m_device(&device) {}

    rhi::Device* m_device;
    rhi::PipelineHandle m_occlusionPipeline;
    rhi::PipelineHandle m_blurPipeline;
    rhi::TextureHandle m_raw;
    rhi::TextureHandle m_blurred;
    rhi::Extent m_frameExtent;
    rhi::Extent m_extent;
};

} // namespace daidalos::renderer
