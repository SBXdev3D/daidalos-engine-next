// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "TemporalResolvePass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/TemporalResolve.cs.spv.h>

#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches ResolvePushConstants in shaders/TemporalResolve.slang.
struct ResolvePushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t color;
    std::uint32_t depth;
    std::uint32_t motion;
    std::uint32_t reactive;
    std::uint32_t history;
    std::uint32_t output;
    std::uint32_t reset;
    std::uint32_t width;
    std::uint32_t height;
};

constexpr std::uint32_t kGroupSize = 8;

/// The radical inverse of index in a base: its digits mirrored around the point, from 0 to 1.
float RadicalInverse(std::uint64_t index, std::uint64_t base) {
    float result = 0.0f;
    float digit = 1.0f / static_cast<float>(base);
    for (; index > 0; index /= base) {
        result += static_cast<float>(index % base) * digit;
        digit /= static_cast<float>(base);
    }
    return result;
}

} // namespace

glm::vec2 Jitter(std::uint64_t frame) {
    // The sequence starts at 1: its first point, 0, would be the corner of the pixel twice.
    const std::uint64_t index = frame % kJitterPositions + 1;
    return {RadicalInverse(index, 2) - 0.5f, RadicalInverse(index, 3) - 0.5f};
}

TemporalResolvePass::TemporalResolvePass(rhi::Device& device) : m_device(&device) {}

TemporalResolvePass::TemporalResolvePass(TemporalResolvePass&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_history(std::exchange(other.m_history, {})), m_current(other.m_current), m_extent(other.m_extent),
      m_hasHistory(other.m_hasHistory) {}

TemporalResolvePass::~TemporalResolvePass() {
    m_device->DestroyPipeline(m_pipeline);
    for (const rhi::TextureHandle history : m_history) {
        m_device->DestroyTexture(history);
    }
}

core::Result<TemporalResolvePass> TemporalResolvePass::Create(rhi::Device& device) {
    TemporalResolvePass pass(device);
    DAI_TRY_ASSIGN(pass.m_pipeline, device.CreateComputePipeline(
                                        {.shader = rhi::ToSpirv(kTemporalResolveCsSpirv), .name = "Temporal resolve"}));
    return pass;
}

core::Result<void> TemporalResolvePass::Resize(rhi::Extent extent) {
    if (m_history[0] && extent == m_extent) {
        return {};
    }
    std::array<rhi::TextureHandle, 2> created;
    for (rhi::TextureHandle& history : created) {
        core::Result<rhi::TextureHandle> texture = m_device->CreateTexture(
            {.extent = extent,
             .format = rhi::Format::Rgba32Float,
             .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
             .name = "Temporal history"});
        if (!texture) {
            m_device->DestroyTexture(created[0]);
            return tl::unexpected(std::move(texture.error()));
        }
        history = *texture;
    }
    for (const rhi::TextureHandle history : m_history) {
        m_device->DestroyTexture(history);
    }
    m_history = created;
    m_extent = extent;
    m_hasHistory = false;
    return {};
}

void TemporalResolvePass::AddTo(RenderGraph& graph, const TemporalResolveInputs& inputs) {
    const rhi::TextureHandle history = m_history[m_current];
    m_current = 1 - m_current;
    const rhi::TextureHandle output = m_history[m_current];
    const ResolvePushConstants push{inputs.frameBuffer,
                                    m_device->BindlessIndex(inputs.color),
                                    m_device->BindlessIndex(inputs.depth),
                                    m_device->BindlessIndex(inputs.motion),
                                    m_device->BindlessIndex(inputs.reactive),
                                    m_device->BindlessIndex(history),
                                    m_device->StorageImageIndex(output),
                                    m_hasHistory ? 0U : 1U,
                                    m_extent.width,
                                    m_extent.height};
    PassDesc desc{.name = "Temporal resolve",
                  .textures = {{output, rhi::Access::ComputeShaderWrite, true},
                               {inputs.color, rhi::Access::ComputeShaderRead},
                               {inputs.depth, rhi::Access::ComputeShaderRead},
                               {inputs.motion, rhi::Access::ComputeShaderRead},
                               {inputs.reactive, rhi::Access::ComputeShaderRead}}};
    // Without history, the shader does not read it: it may never have been written.
    if (m_hasHistory) {
        desc.textures.push_back({history, rhi::Access::ComputeShaderRead});
    }
    graph.AddPass(std::move(desc), [this, push](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::TemporalResolve");
        commands.BindPipeline(m_pipeline);
        commands.Push(push);
        commands.Dispatch((push.width + kGroupSize - 1) / kGroupSize, (push.height + kGroupSize - 1) / kGroupSize);
    });
    m_hasHistory = true;
}

} // namespace daidalos::renderer
