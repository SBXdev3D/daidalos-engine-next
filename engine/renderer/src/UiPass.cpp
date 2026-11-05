// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "UiPass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/vec2.hpp>
#include <shaders/Ui.ps.spv.h>
#include <shaders/Ui.vs.spv.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace daidalos::renderer {

namespace {

// Matches PushConstants in shaders/Ui.hlsl.
struct PushConstants {
    glm::vec2 scale;
    glm::vec2 translate;
    std::uint32_t texture;
    std::uint32_t shading;
};

} // namespace

core::Result<UiPass> UiPass::Create(rhi::Device& device, rhi::Format colorFormat) {
    UiPass pass(device);
    DAI_TRY_ASSIGN(
        pass.m_pipeline,
        device.CreatePipeline({
            .vertexShader = rhi::ToSpirv(kUiVsSpirv),
            .fragmentShader = rhi::ToSpirv(kUiPsSpirv),
            .vertexStride = sizeof(ui::Vertex),
            .vertexAttributes = {{0, rhi::Format::Rg32Float,
                                  static_cast<std::uint32_t>(offsetof(ui::Vertex, position))},
                                 {1, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(ui::Vertex, uv))},
                                 {2, rhi::Format::Rgba8Unorm, static_cast<std::uint32_t>(offsetof(ui::Vertex, color))}},
            .colorFormat = colorFormat,
            .depthTest = false,
            .depthWrite = false,
            .cull = rhi::CullMode::None,
            .blend = rhi::BlendMode::Alpha,
            .name = "Interface",
        }));
    return pass;
}

UiPass::UiPass(UiPass&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_frames(std::exchange(other.m_frames, {})), m_frameIndex(other.m_frameIndex) {}

UiPass::~UiPass() {
    for (const Geometry& geometry : m_frames) {
        m_device->DestroyBuffer(geometry.vertices);
        m_device->DestroyBuffer(geometry.indices);
    }
    m_device->DestroyPipeline(m_pipeline);
}

core::Result<void> UiPass::Reserve(Geometry& geometry, std::size_t vertices, std::size_t indices) {
    // The old buffers may be in use by the GPU: their destruction is deferred.
    if (vertices > geometry.vertexCapacity) {
        const std::size_t capacity = std::max(vertices * 2, std::size_t{1024});
        m_device->DestroyBuffer(geometry.vertices);
        geometry.vertices = {};
        geometry.vertexCapacity = 0;
        DAI_TRY_ASSIGN(geometry.vertices, m_device->CreateBuffer({.size = capacity * sizeof(ui::Vertex),
                                                                  .usage = rhi::BufferUsage::Vertex,
                                                                  .memory = rhi::MemoryLocation::CpuToGpu,
                                                                  .name = "Interface vertices"}));
        geometry.vertexCapacity = capacity;
    }
    if (indices > geometry.indexCapacity) {
        const std::size_t capacity = std::max(indices * 2, std::size_t{1536});
        m_device->DestroyBuffer(geometry.indices);
        geometry.indices = {};
        geometry.indexCapacity = 0;
        DAI_TRY_ASSIGN(geometry.indices, m_device->CreateBuffer({.size = capacity * sizeof(std::uint32_t),
                                                                 .usage = rhi::BufferUsage::Index,
                                                                 .memory = rhi::MemoryLocation::CpuToGpu,
                                                                 .name = "Interface indices"}));
        geometry.indexCapacity = capacity;
    }
    return {};
}

core::Result<void> UiPass::Record(rhi::CommandList& commands, const ui::Canvas& canvas) {
    DAI_PROFILE_SCOPE("renderer::UiPass::Record");
    const glm::vec2 size = canvas.Size();
    if (canvas.Commands().empty() || size.x <= 0.0f || size.y <= 0.0f) {
        return {};
    }
    Geometry& geometry = m_frames[m_frameIndex];
    m_frameIndex = (m_frameIndex + 1) % rhi::kFramesInFlight;
    DAI_TRY(Reserve(geometry, canvas.Vertices().size(), canvas.Indices().size()));
    std::memcpy(m_device->Mapped(geometry.vertices).data(), canvas.Vertices().data(), canvas.Vertices().size_bytes());
    std::memcpy(m_device->Mapped(geometry.indices).data(), canvas.Indices().data(), canvas.Indices().size_bytes());

    // Y goes down on the canvas and up in clip space (the viewport flips it once, CONVENTIONS.md section 5).
    const glm::vec2 scale(2.0f / size.x, -2.0f / size.y);
    commands.BindPipeline(m_pipeline);
    commands.BindVertexBuffer(geometry.vertices);
    commands.BindIndexBuffer(geometry.indices, rhi::IndexType::Uint32);
    for (const ui::DrawCommand& command : canvas.Commands()) {
        const std::uint32_t texture = command.texture ? m_device->BindlessIndex(command.texture) : 0;
        commands.Push(
            PushConstants{scale, glm::vec2(-1.0f, 1.0f), texture, static_cast<std::uint32_t>(command.shading)});
        commands.DrawIndexed(command.indexCount, command.firstIndex, 0);
    }
    return {};
}

} // namespace daidalos::renderer
