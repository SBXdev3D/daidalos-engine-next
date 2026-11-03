// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

namespace daidalos::renderer {

/// Fast approximate antialiasing, after tonemapping (ADR-0026). The Device must outlive it.
class FxaaPass {
public:
    /// What the tonemap pass writes for it: sRGB-encoded colors, with their luma in alpha.
    static constexpr rhi::Format kSourceFormat = rhi::Format::Rgba8Unorm;

    /// targetFormat is an sRGB format, such as the swapchain's.
    static core::Result<FxaaPass> Create(rhi::Device& device, rhi::Format targetFormat);

    FxaaPass(FxaaPass&& other) noexcept;
    FxaaPass& operator=(FxaaPass&& other) = delete;
    FxaaPass(const FxaaPass&) = delete;
    FxaaPass& operator=(const FxaaPass&) = delete;
    ~FxaaPass();

    /// Draws the antialiased source over the whole target, inside a rendering scope. The source, of
    /// kSourceFormat and of the target's size, must be in the ShaderRead state.
    void Record(rhi::CommandList& commands, rhi::TextureHandle source, rhi::Extent extent);

private:
    explicit FxaaPass(rhi::Device& device) : m_device(&device) {}

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
};

} // namespace daidalos::renderer
