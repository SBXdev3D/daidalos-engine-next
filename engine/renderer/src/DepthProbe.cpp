// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "DepthProbe.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/DepthProbe.cs.spv.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr std::uint32_t kGroupSize = 8;
constexpr std::uint32_t kMaxCells = DepthProbe::kMaxWidth * DepthProbe::kMaxWidth;
constexpr std::uint64_t kGridBytes = std::uint64_t{kMaxCells} * sizeof(float);

struct DepthProbePushConstants {
    std::uint32_t depth;
    std::uint32_t output;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t renderWidth;
    std::uint32_t renderHeight;
    float nearPlane;
    float farPlane;
};

} // namespace

DepthProbe::DepthProbe(DepthProbe&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_grid(std::exchange(other.m_grid, {})), m_slots(std::exchange(other.m_slots, {})),
      m_latest(std::move(other.m_latest)) {}

DepthProbe::~DepthProbe() {
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyBuffer(m_grid);
    for (const Slot& slot : m_slots) {
        m_device->DestroyBuffer(slot.readback);
    }
}

core::Result<DepthProbe> DepthProbe::Create(rhi::Device& device) {
    DepthProbe probe(device);
    DAI_TRY_ASSIGN(probe.m_pipeline,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kDepthProbeCsSpirv), .name = "Depth probe"}));
    DAI_TRY_ASSIGN(probe.m_grid,
                   device.CreateBuffer({.size = kGridBytes,
                                        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferSrc,
                                        .name = "Depth probe"}));
    for (Slot& slot : probe.m_slots) {
        DAI_TRY_ASSIGN(slot.readback, device.CreateBuffer({.size = kGridBytes,
                                                           .usage = rhi::BufferUsage::TransferDst,
                                                           .memory = rhi::MemoryLocation::GpuToCpu,
                                                           .name = "Depth probe readback"}));
    }
    return probe;
}

void DepthProbe::Read(std::uint32_t slot) {
    Slot& read = m_slots[slot];
    if (!read.written) {
        return;
    }
    read.written = false;
    DepthPicture picture{.width = read.width, .height = read.height, .viewProjection = read.viewProjection};
    picture.meters.resize(std::size_t{read.width} * read.height);
    const std::span<const std::byte> mapped = m_device->Mapped(read.readback);
    std::memcpy(picture.meters.data(), mapped.data(), picture.meters.size() * sizeof(float));
    m_latest = std::move(picture);
}

void DepthProbe::AddTo(RenderGraph& graph, std::uint32_t slot, rhi::TextureHandle depth, rhi::Extent renderExtent,
                       float nearPlane, float farPlane, const glm::mat4& viewProjection) {
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        return;
    }
    // As wide as kMaxWidth at most, with the frame's shape.
    const std::uint32_t width = std::min(kMaxWidth, renderExtent.width);
    const std::uint32_t height = std::clamp(
        static_cast<std::uint32_t>(std::lround(static_cast<double>(width) * renderExtent.height / renderExtent.width)),
        1U, kMaxWidth);
    const DepthProbePushConstants push{.depth = m_device->BindlessIndex(depth),
                                       .output = m_device->StorageIndex(m_grid),
                                       .width = width,
                                       .height = height,
                                       .renderWidth = renderExtent.width,
                                       .renderHeight = renderExtent.height,
                                       .nearPlane = nearPlane,
                                       .farPlane = farPlane};
    graph.AddPass({.name = "Depth probe",
                   .textures = {{depth, rhi::Access::ComputeShaderRead}},
                   .buffers = {{m_grid, rhi::Access::ComputeShaderWrite}}},
                  [this, push](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::DepthProbe");
                      commands.BindPipeline(m_pipeline);
                      commands.Push(push);
                      commands.Dispatch((push.width + kGroupSize - 1) / kGroupSize,
                                        (push.height + kGroupSize - 1) / kGroupSize);
                  });
    Slot& written = m_slots[slot];
    const rhi::BufferHandle readback = written.readback;
    const std::uint64_t bytes = std::uint64_t{width} * height * sizeof(float);
    graph.AddPass(
        {.name = "Depth probe readback",
         .group = "Depth probe",
         .buffers = {{m_grid, rhi::Access::TransferRead}, {readback, rhi::Access::TransferWrite}}},
        [this, readback, bytes](rhi::CommandList& commands) { commands.CopyBuffer(m_grid, readback, bytes); });
    written.written = true;
    written.width = width;
    written.height = height;
    written.viewProjection = viewProjection;
}

} // namespace daidalos::renderer
