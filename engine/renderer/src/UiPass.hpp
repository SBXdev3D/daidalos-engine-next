// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/CommandList.hpp>
#include <daidalos/rhi/Device.hpp>
#include <daidalos/ui/Canvas.hpp>

#include <array>
#include <cstddef>

namespace daidalos::renderer {

/// Draws a ui::Canvas into the back buffer (ADR-0027), from buffers that it refills each frame.
class UiPass {
public:
    static core::Result<UiPass> Create(rhi::Device& device, rhi::Format colorFormat);

    UiPass(UiPass&& other) noexcept;
    UiPass& operator=(UiPass&& other) = delete;
    UiPass(const UiPass&) = delete;
    UiPass& operator=(const UiPass&) = delete;
    ~UiPass();

    /// Inside a rendering scope over the back buffer. An error means that a buffer could not grow.
    [[nodiscard]] core::Result<void> Record(rhi::CommandList& commands, const ui::Canvas& canvas);

private:
    struct Geometry {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::size_t vertexCapacity = 0;
        std::size_t indexCapacity = 0;
    };

    explicit UiPass(rhi::Device& device) : m_device(&device) {}
    [[nodiscard]] core::Result<void> Reserve(Geometry& geometry, std::size_t vertices, std::size_t indices);

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    std::array<Geometry, rhi::kFramesInFlight> m_frames;
    std::uint32_t m_frameIndex = 0;
};

} // namespace daidalos::renderer
