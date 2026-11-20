// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <optional>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

/// A small grid of the scene's depth in meters, read back for an editor's overlays (ADR-0106).
class DepthProbe {
public:
    static constexpr std::uint32_t kMaxWidth = 320;

    static core::Result<DepthProbe> Create(rhi::Device& device);

    DepthProbe(DepthProbe&& other) noexcept;
    DepthProbe& operator=(DepthProbe&& other) = delete;
    DepthProbe(const DepthProbe&) = delete;
    DepthProbe& operator=(const DepthProbe&) = delete;
    ~DepthProbe();

    /// At the start of a frame of this slot, after BeginFrame: takes the grid its last frame wrote.
    void Read(std::uint32_t slot);
    /// Adds the pass that writes the grid from the G-buffer's depth, and its copy for the CPU.
    void AddTo(RenderGraph& graph, std::uint32_t slot, rhi::TextureHandle depth, rhi::Extent renderExtent,
               float nearPlane, float farPlane, const glm::mat4& viewProjection);
    /// The latest grid read back, if any.
    [[nodiscard]] const std::optional<DepthPicture>& Latest() const { return m_latest; }

private:
    explicit DepthProbe(rhi::Device& device) : m_device(&device) {}

    struct Slot {
        rhi::BufferHandle readback;
        bool written = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        glm::mat4 viewProjection{1.0f};
    };

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    rhi::BufferHandle m_grid;
    std::array<Slot, rhi::kFramesInFlight> m_slots{};
    std::optional<DepthPicture> m_latest;
};

} // namespace daidalos::renderer
