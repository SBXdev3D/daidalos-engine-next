// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Bloom.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/Bloom.cs.spv.h>

#include <algorithm>
#include <format>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches BloomPushConstants in shaders/Bloom.slang.
struct BloomPushConstants {
    std::uint32_t source;
    std::uint32_t add;
    std::uint32_t output;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mode;
    float scale;
};

constexpr std::uint32_t kDown = 0;
constexpr std::uint32_t kDownKaris = 1;
constexpr std::uint32_t kUp = 2;
constexpr std::uint32_t kGroupSize = 8;

} // namespace

Bloom::Bloom(rhi::Device& device) : m_device(&device) {}

Bloom::Bloom(Bloom&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_down(std::exchange(other.m_down, {})), m_up(std::exchange(other.m_up, {})), m_extents(other.m_extents),
      m_extent(other.m_extent) {}

Bloom::~Bloom() {
    m_device->DestroyPipeline(m_pipeline);
    DestroyLevels();
}

void Bloom::DestroyLevels() {
    for (rhi::TextureHandle& level : m_down) {
        m_device->DestroyTexture(level);
        level = {};
    }
    for (rhi::TextureHandle& level : m_up) {
        m_device->DestroyTexture(level);
        level = {};
    }
}

core::Result<Bloom> Bloom::Create(rhi::Device& device) {
    Bloom bloom(device);
    DAI_TRY_ASSIGN(bloom.m_pipeline,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kBloomCsSpirv), .name = "Bloom"}));
    return bloom;
}

core::Result<void> Bloom::Resize(rhi::Extent extent) {
    if (m_down[0] && extent == m_extent) {
        return {};
    }
    DestroyLevels();
    m_extent = extent;
    for (std::uint32_t level = 0; level < kLevels; ++level) {
        m_extents[level] = {std::max(extent.width >> (level + 1), 1U), std::max(extent.height >> (level + 1), 1U)};
        const rhi::TextureDesc desc{.extent = m_extents[level],
                                    .format = rhi::Format::Rgba16Float,
                                    .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
                                    .name = std::format("Bloom {}", level)};
        DAI_TRY_ASSIGN(m_down[level], m_device->CreateTexture(desc));
        if (level + 1 < kLevels) {
            DAI_TRY_ASSIGN(m_up[level], m_device->CreateTexture(desc));
        }
    }
    return {};
}

void Bloom::AddTo(RenderGraph& graph, rhi::TextureHandle image) {
    const auto dispatch = [this](rhi::CommandList& commands, const BloomPushConstants& push) {
        commands.BindPipeline(m_pipeline);
        commands.Push(push);
        commands.Dispatch((push.width + kGroupSize - 1) / kGroupSize, (push.height + kGroupSize - 1) / kGroupSize);
    };
    for (std::uint32_t level = 0; level < kLevels; ++level) {
        const rhi::TextureHandle source = level == 0 ? image : m_down[level - 1];
        const BloomPushConstants push{.source = m_device->BindlessIndex(source),
                                      .add = 0,
                                      .output = m_device->StorageImageIndex(m_down[level]),
                                      .width = m_extents[level].width,
                                      .height = m_extents[level].height,
                                      .mode = level == 0 ? kDownKaris : kDown,
                                      .scale = 1.0f};
        graph.AddPass({.name = "Bloom down",
                       .group = "Bloom",
                       .textures = {{m_down[level], rhi::Access::ComputeShaderWrite, true},
                                    {source, rhi::Access::ComputeShaderRead}}},
                      [dispatch, push](rhi::CommandList& commands) { dispatch(commands, push); });
    }
    // Back up: each level adds the blur of the one below to its own image. The largest level averages the
    // levels, so that the bloom is as bright as the image.
    for (std::uint32_t level = kLevels - 1; level-- > 0;) {
        const rhi::TextureHandle smaller = level + 2 == kLevels ? m_down[level + 1] : m_up[level + 1];
        const BloomPushConstants push{.source = m_device->BindlessIndex(smaller),
                                      .add = m_device->BindlessIndex(m_down[level]),
                                      .output = m_device->StorageImageIndex(m_up[level]),
                                      .width = m_extents[level].width,
                                      .height = m_extents[level].height,
                                      .mode = kUp,
                                      .scale = level == 0 ? 1.0f / static_cast<float>(kLevels) : 1.0f};
        graph.AddPass({.name = "Bloom up",
                       .group = "Bloom",
                       .textures = {{m_up[level], rhi::Access::ComputeShaderWrite, true},
                                    {smaller, rhi::Access::ComputeShaderRead},
                                    {m_down[level], rhi::Access::ComputeShaderRead}}},
                      [dispatch, push](rhi::CommandList& commands) { dispatch(commands, push); });
    }
}

} // namespace daidalos::renderer
