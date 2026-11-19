// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Denoiser.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/DenoiseSpatial.cs.spv.h>
#include <shaders/DenoiseTemporal.cs.spv.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches TemporalPushConstants in shaders/DenoiseTemporal.slang.
struct TemporalPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t diffuse;
    std::uint32_t specular;
    std::uint32_t depth;
    std::uint32_t normals;
    std::uint32_t motion;
    std::uint32_t previousGuides;
    std::uint32_t previousMoments;
    std::uint32_t previousDiffuse;
    std::uint32_t previousSpecular;
    std::uint32_t guides;
    std::uint32_t moments;
    std::uint32_t historyDiffuse;
    std::uint32_t historySpecular;
    std::uint32_t reset;
    std::uint32_t reflections;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t diffuseRepeats;
    std::uint32_t specularRepeats;
};

// Matches SpatialPushConstants in shaders/DenoiseSpatial.slang.
struct SpatialPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t depth;
    std::uint32_t normals;
    std::uint32_t guides;
    std::uint32_t inputDiffuse;
    std::uint32_t inputSpecular;
    std::uint32_t outputDiffuse;
    std::uint32_t outputSpecular;
    std::uint32_t step;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t diffuseRepeats;
    std::uint32_t specularRepeats;
};

constexpr std::uint32_t kGroupSize = 8;
constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;
constexpr rhi::Format kFormat = rhi::Format::Rgba16Float;

} // namespace

Denoiser::Denoiser(rhi::Device& device) : m_device(&device) {}

Denoiser::Denoiser(Denoiser&& other) noexcept
    : m_device(other.m_device), m_temporal(std::exchange(other.m_temporal, {})),
      m_spatial(std::exchange(other.m_spatial, {})), m_diffuse(std::exchange(other.m_diffuse, {})),
      m_specular(std::exchange(other.m_specular, {})), m_guides(std::exchange(other.m_guides, {})),
      m_moments(std::exchange(other.m_moments, {})), m_current(other.m_current), m_extent(other.m_extent),
      m_hasHistory(other.m_hasHistory) {}

Denoiser::~Denoiser() {
    m_device->DestroyPipeline(m_temporal);
    m_device->DestroyPipeline(m_spatial);
    DestroyTextures();
}

void Denoiser::DestroyTextures() {
    for (Signal* signal : {&m_diffuse, &m_specular}) {
        for (const rhi::TextureHandle texture : signal->history) {
            m_device->DestroyTexture(texture);
        }
        for (const rhi::TextureHandle texture : signal->work) {
            m_device->DestroyTexture(texture);
        }
        *signal = {};
    }
    for (std::array<rhi::TextureHandle, 2>* pair : {&m_guides, &m_moments}) {
        for (const rhi::TextureHandle texture : *pair) {
            m_device->DestroyTexture(texture);
        }
        *pair = {};
    }
}

core::Result<Denoiser> Denoiser::Create(rhi::Device& device) {
    Denoiser denoiser(device);
    DAI_TRY_ASSIGN(denoiser.m_temporal, device.CreateComputePipeline({.shader = rhi::ToSpirv(kDenoiseTemporalCsSpirv),
                                                                      .name = "Denoiser temporal accumulation"}));
    DAI_TRY_ASSIGN(denoiser.m_spatial, device.CreateComputePipeline({.shader = rhi::ToSpirv(kDenoiseSpatialCsSpirv),
                                                                     .name = "Denoiser spatial filter"}));
    return denoiser;
}

core::Result<void> Denoiser::Resize(rhi::Extent extent) {
    if (m_guides[0] && extent == m_extent) {
        return {};
    }
    DestroyTextures();
    m_hasHistory = false;
    const auto create = [&](rhi::TextureHandle& texture, const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(texture,
                       m_device->CreateTexture({.extent = extent,
                                                .format = kFormat,
                                                .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled |
                                                         rhi::TextureUsage::TransferSrc,
                                                .name = name}));
        return {};
    };
    core::Result<void> created = {};
    for (std::uint32_t index = 0; index < 2 && created; ++index) {
        created = create(m_diffuse.history[index], "Denoiser diffuse history")
                      .and_then([&] { return create(m_diffuse.work[index], "Denoiser diffuse"); })
                      .and_then([&] { return create(m_specular.history[index], "Denoiser specular history"); })
                      .and_then([&] { return create(m_specular.work[index], "Denoiser specular"); })
                      .and_then([&] { return create(m_guides[index], "Denoiser guides"); })
                      .and_then([&] { return create(m_moments[index], "Denoiser moments"); });
    }
    if (!created) {
        DestroyTextures();
        return created;
    }
    m_extent = extent;
    return {};
}

void Denoiser::AddTo(RenderGraph& graph, const DenoiserInputs& inputs) {
    const rhi::Extent extent = inputs.extent.width > 0 ? inputs.extent : m_extent;
    const std::uint32_t previous = m_current;
    m_current = 1 - m_current;
    const auto sampled = [this](rhi::TextureHandle texture) { return m_device->BindlessIndex(texture); };
    const auto storage = [this](rhi::TextureHandle texture) { return m_device->StorageImageIndex(texture); };
    const std::uint32_t groupsX = (extent.width + kGroupSize - 1) / kGroupSize;
    const std::uint32_t groupsY = (extent.height + kGroupSize - 1) / kGroupSize;

    // Temporal accumulation, into this frame's history.
    const TemporalPushConstants temporal{inputs.frameBuffer,
                                         sampled(inputs.diffuse),
                                         sampled(inputs.specular),
                                         sampled(inputs.depth),
                                         sampled(inputs.normals),
                                         sampled(inputs.motion),
                                         sampled(m_guides[previous]),
                                         sampled(m_moments[previous]),
                                         sampled(m_diffuse.history[previous]),
                                         sampled(m_specular.history[previous]),
                                         storage(m_guides[m_current]),
                                         storage(m_moments[m_current]),
                                         storage(m_diffuse.history[m_current]),
                                         storage(m_specular.history[m_current]),
                                         m_hasHistory ? 0U : 1U,
                                         inputs.reflections ? sampled(inputs.reflections) : kNoTexture,
                                         extent.width,
                                         extent.height,
                                         std::max(inputs.diffuseRepeats, 1U),
                                         std::max(inputs.specularRepeats, 1U)};
    PassDesc accumulation{.name = "Denoiser temporal accumulation",
                          .group = "Denoiser",
                          .textures = {{m_guides[m_current], rhi::Access::ComputeShaderWrite, true},
                                       {m_moments[m_current], rhi::Access::ComputeShaderWrite, true},
                                       {m_diffuse.history[m_current], rhi::Access::ComputeShaderWrite, true},
                                       {m_specular.history[m_current], rhi::Access::ComputeShaderWrite, true},
                                       {inputs.diffuse, rhi::Access::ComputeShaderRead},
                                       {inputs.specular, rhi::Access::ComputeShaderRead},
                                       {inputs.depth, rhi::Access::ComputeShaderRead},
                                       {inputs.normals, rhi::Access::ComputeShaderRead},
                                       {inputs.motion, rhi::Access::ComputeShaderRead},
                                       {inputs.reflections, rhi::Access::ComputeShaderRead}}};
    // Without history, the shader does not read it: it may never have been written.
    if (m_hasHistory) {
        for (const rhi::TextureHandle texture :
             {m_guides[previous], m_moments[previous], m_diffuse.history[previous], m_specular.history[previous]}) {
            accumulation.textures.push_back({texture, rhi::Access::ComputeShaderRead});
        }
    }
    graph.AddPass(std::move(accumulation), [this, temporal, groupsX, groupsY](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::Denoiser::Temporal");
        commands.BindPipeline(m_temporal);
        commands.Push(temporal);
        commands.Dispatch(groupsX, groupsY);
    });

    // The spatial filter reads the history and goes back and forth between the work textures: history ->
    // work[0] -> work[1] -> work[0], the output.
    static_assert(kIterations % 2 == 1, "The last iteration writes work[0]");
    for (std::uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        const auto source = [&](const Signal& signal) {
            return iteration == 0 ? signal.history[m_current] : signal.work[(iteration - 1) % 2];
        };
        const auto target = [&](const Signal& signal) { return signal.work[iteration % 2]; };
        const SpatialPushConstants spatial{inputs.frameBuffer,
                                           sampled(inputs.depth),
                                           sampled(inputs.normals),
                                           sampled(m_guides[m_current]),
                                           sampled(source(m_diffuse)),
                                           sampled(source(m_specular)),
                                           storage(target(m_diffuse)),
                                           storage(target(m_specular)),
                                           1U << iteration,
                                           extent.width,
                                           extent.height,
                                           std::max(inputs.diffuseRepeats, 1U),
                                           std::max(inputs.specularRepeats, 1U)};
        graph.AddPass({.name = "Denoiser spatial filter",
                       .group = "Denoiser",
                       .textures = {{target(m_diffuse), rhi::Access::ComputeShaderWrite, true},
                                    {target(m_specular), rhi::Access::ComputeShaderWrite, true},
                                    {source(m_diffuse), rhi::Access::ComputeShaderRead},
                                    {source(m_specular), rhi::Access::ComputeShaderRead},
                                    {m_guides[m_current], rhi::Access::ComputeShaderRead},
                                    {inputs.depth, rhi::Access::ComputeShaderRead},
                                    {inputs.normals, rhi::Access::ComputeShaderRead}}},
                      [this, spatial, groupsX, groupsY](rhi::CommandList& commands) {
                          DAI_PROFILE_SCOPE("renderer::Denoiser::Spatial");
                          commands.BindPipeline(m_spatial);
                          commands.Push(spatial);
                          commands.Dispatch(groupsX, groupsY);
                      });
    }
    m_hasHistory = true;
}

} // namespace daidalos::renderer
