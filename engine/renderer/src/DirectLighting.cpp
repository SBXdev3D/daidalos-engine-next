// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "DirectLighting.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/RestirCandidates.cs.spv.h>
#include <shaders/RestirShade.cs.spv.h>
#include <shaders/RestirSpatial.cs.spv.h>
#include <shaders/RestirTemporal.cs.spv.h>

#include <utility>
#include <vector>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches RestirPushConstants in shaders/Restir.slangh.
struct RestirPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t depth;
    std::uint32_t normals;
    std::uint32_t baseColor;
    std::uint32_t emission;
    std::uint32_t motion;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t reservoirs;
    std::uint32_t previousReservoirs;
    std::uint32_t lightMap;
    std::uint32_t previousLightCount;
    std::uint32_t guides;
    std::uint32_t previousGuides;
    std::uint32_t diffuse;
    std::uint32_t specular;
    std::uint32_t debug;
    std::uint32_t seed;
    std::uint32_t flags;
    std::uint32_t padding;
};
static_assert(sizeof(RestirPushConstants) <= rhi::kMaxPushConstantSize);

// Match the flags in shaders/Restir.slangh.
constexpr std::uint32_t kTemporal = 1;
constexpr std::uint32_t kSpatial = 2;
constexpr std::uint32_t kUnbiased = 4;
constexpr std::uint32_t kDebug = 8;
constexpr std::uint32_t kIndirect = 16;

constexpr std::uint32_t kGroupSize = 8;
constexpr rhi::Format kGuideFormat = rhi::Format::Rgba16Float;

} // namespace

DirectLighting::DirectLighting(DirectLighting&& other) noexcept
    : m_device(other.m_device), m_candidates(std::exchange(other.m_candidates, {})),
      m_temporal(std::exchange(other.m_temporal, {})), m_spatial(std::exchange(other.m_spatial, {})),
      m_shade(std::exchange(other.m_shade, {})), m_reservoirs(std::exchange(other.m_reservoirs, {})),
      m_finalReservoirs(std::exchange(other.m_finalReservoirs, {})), m_guides(std::exchange(other.m_guides, {})),
      m_debug(std::exchange(other.m_debug, {})), m_current(other.m_current), m_extent(other.m_extent),
      m_hasHistory(other.m_hasHistory) {}

DirectLighting::~DirectLighting() {
    for (const rhi::PipelineHandle pipeline : {m_candidates, m_temporal, m_spatial, m_shade}) {
        m_device->DestroyPipeline(pipeline);
    }
    DestroyResources();
}

void DirectLighting::DestroyResources() {
    m_device->DestroyBuffer(m_reservoirs);
    m_device->DestroyBuffer(m_finalReservoirs);
    for (const rhi::TextureHandle texture : m_guides) {
        m_device->DestroyTexture(texture);
    }
    m_device->DestroyTexture(m_debug);
    m_reservoirs = {};
    m_finalReservoirs = {};
    m_guides = {};
    m_debug = {};
}

core::Result<DirectLighting> DirectLighting::Create(rhi::Device& device) {
    DirectLighting lighting(device);
    const auto create = [&](rhi::PipelineHandle& pipeline, std::span<const std::uint32_t> shader,
                            const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(pipeline, device.CreateComputePipeline({.shader = shader, .name = name}));
        return {};
    };
    DAI_TRY(create(lighting.m_candidates, rhi::ToSpirv(kRestirCandidatesCsSpirv), "ReSTIR candidates"));
    DAI_TRY(create(lighting.m_temporal, rhi::ToSpirv(kRestirTemporalCsSpirv), "ReSTIR temporal reuse"));
    DAI_TRY(create(lighting.m_spatial, rhi::ToSpirv(kRestirSpatialCsSpirv), "ReSTIR spatial reuse"));
    DAI_TRY(create(lighting.m_shade, rhi::ToSpirv(kRestirShadeCsSpirv), "Lighting"));
    return lighting;
}

core::Result<void> DirectLighting::Resize(rhi::Extent extent) {
    if (m_reservoirs && extent == m_extent) {
        return {};
    }
    DestroyResources();
    m_hasHistory = false;
    const std::uint64_t reservoirBytes = std::uint64_t{extent.width} * extent.height * kReservoirSize;
    const auto buffer = [&](rhi::BufferHandle& created, const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(created, m_device->CreateBuffer(
                                    {.size = reservoirBytes, .usage = rhi::BufferUsage::Storage, .name = name}));
        return {};
    };
    const auto texture = [&](rhi::TextureHandle& created, rhi::Format format, const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(created, m_device->CreateTexture({.extent = extent,
                                                         .format = format,
                                                         .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
                                                         .name = name}));
        return {};
    };
    core::Result<void> created = buffer(m_reservoirs, "Reservoirs")
                                     .and_then([&] { return buffer(m_finalReservoirs, "Reused reservoirs"); })
                                     .and_then([&] { return texture(m_guides[0], kGuideFormat, "Lighting guides"); })
                                     .and_then([&] { return texture(m_guides[1], kGuideFormat, "Lighting guides"); })
                                     .and_then([&] { return texture(m_debug, kDebugFormat, "Lights debug view"); });
    if (!created) {
        DestroyResources();
        return created;
    }
    m_extent = extent;
    return {};
}

void DirectLighting::AddTo(RenderGraph& graph, const DirectLightingInputs& inputs,
                           const DirectLightingSettings& settings) {
    const std::uint32_t previous = m_current;
    m_current = 1 - m_current;
    const auto sampled = [this](rhi::TextureHandle texture) { return m_device->BindlessIndex(texture); };
    const auto storage = [this](rhi::TextureHandle texture) { return m_device->StorageImageIndex(texture); };
    std::uint32_t flags = (settings.spatial ? kSpatial : 0) | (settings.unbiased ? kUnbiased : 0) |
                          (settings.debug ? kDebug : 0) | (m_hasHistory ? kTemporal : 0) |
                          (inputs.indirect ? kIndirect : 0);
    RestirPushConstants push{inputs.frameBuffer,
                             inputs.instanceBuffer,
                             inputs.materialBuffer,
                             sampled(inputs.depth),
                             sampled(inputs.normals),
                             sampled(inputs.baseColor),
                             sampled(inputs.emission),
                             sampled(inputs.motion),
                             m_extent.width,
                             m_extent.height,
                             m_device->StorageIndex(m_reservoirs),
                             m_device->StorageIndex(m_finalReservoirs),
                             inputs.lightMap,
                             inputs.previousLightCount,
                             storage(m_guides[m_current]),
                             sampled(m_guides[previous]),
                             storage(inputs.diffuse),
                             storage(inputs.specular),
                             storage(m_debug),
                             inputs.frameIndex,
                             flags,
                             0};
    const std::uint32_t groupsX = (m_extent.width + kGroupSize - 1) / kGroupSize;
    const std::uint32_t groupsY = (m_extent.height + kGroupSize - 1) / kGroupSize;
    const auto dispatch = [this, groupsX, groupsY](rhi::PipelineHandle pipeline, const RestirPushConstants& constants) {
        return [this, pipeline, constants, groupsX, groupsY](rhi::CommandList& commands) {
            DAI_PROFILE_SCOPE("renderer::DirectLighting");
            commands.BindPipeline(pipeline);
            commands.Push(constants);
            commands.Dispatch(groupsX, groupsY);
        };
    };
    // What every pass reads: the G-buffer, and rays.
    const std::vector<rhi::TextureUse> gbuffer{{inputs.depth, rhi::Access::ComputeShaderRead},
                                               {inputs.normals, rhi::Access::ComputeShaderRead},
                                               {inputs.baseColor, rhi::Access::ComputeShaderRead},
                                               {inputs.emission, rhi::Access::ComputeShaderRead}};
    const rhi::BufferUse skinned{inputs.skinnedVertices, rhi::Access::ComputeShaderRead};
    const rhi::BufferUse lights{inputs.emissiveTriangles, rhi::Access::ComputeShaderRead};
    const std::vector<rhi::AccelerationStructureUse> structures{{inputs.structure, rhi::Access::RayQueryRead}};

    PassDesc candidates{.name = "ReSTIR candidates",
                        .group = "Direct lighting",
                        .textures = gbuffer,
                        .buffers = {{m_reservoirs, rhi::Access::ComputeShaderWrite}, skinned, lights},
                        .structures = structures};
    candidates.textures.push_back({m_guides[m_current], rhi::Access::ComputeShaderWrite, true});
    graph.AddPass(std::move(candidates), dispatch(m_candidates, push));

    // Reads the last frame's reused reservoirs and guides, and its own reservoirs, which it writes.
    PassDesc temporal{.name = "ReSTIR temporal reuse",
                      .group = "Direct lighting",
                      .textures = gbuffer,
                      .buffers = {{m_reservoirs, rhi::Access::ComputeShaderWrite}, skinned, lights},
                      .structures = structures};
    temporal.textures.push_back({inputs.motion, rhi::Access::ComputeShaderRead});
    if (m_hasHistory) {
        temporal.textures.push_back({m_guides[previous], rhi::Access::ComputeShaderRead});
        temporal.buffers.push_back({m_finalReservoirs, rhi::Access::ComputeShaderRead});
    }
    graph.AddPass(std::move(temporal), dispatch(m_temporal, push));

    // From the reservoirs into the reused ones.
    RestirPushConstants spatialPush = push;
    spatialPush.reservoirs = m_device->StorageIndex(m_finalReservoirs);
    spatialPush.previousReservoirs = m_device->StorageIndex(m_reservoirs);
    graph.AddPass({.name = "ReSTIR spatial reuse",
                   .group = "Direct lighting",
                   .textures = gbuffer,
                   .buffers = {{m_finalReservoirs, rhi::Access::ComputeShaderWrite},
                               {m_reservoirs, rhi::Access::ComputeShaderRead},
                               skinned,
                               lights},
                   .structures = structures},
                  dispatch(m_spatial, spatialPush));

    RestirPushConstants shadePush = push;
    shadePush.reservoirs = m_device->StorageIndex(m_finalReservoirs);
    PassDesc shade{.name = "Lighting",
                   .group = "Direct lighting",
                   .textures = gbuffer,
                   .buffers = {{m_finalReservoirs, rhi::Access::ComputeShaderWrite}, skinned, lights},
                   .structures = structures};
    shade.textures.push_back({inputs.diffuse, rhi::Access::ComputeShaderWrite, true});
    shade.textures.push_back({inputs.specular, rhi::Access::ComputeShaderWrite, true});
    shade.textures.push_back({inputs.indirect, rhi::Access::ComputeShaderRead});
    shade.textures.push_back({inputs.reflections, rhi::Access::ComputeShaderRead});
    if (settings.debug) {
        shade.textures.push_back({m_debug, rhi::Access::ComputeShaderWrite, true});
    }
    graph.AddPass(std::move(shade), dispatch(m_shade, shadePush));
    m_hasHistory = true;
}

} // namespace daidalos::renderer
