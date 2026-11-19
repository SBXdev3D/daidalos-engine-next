// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "GlobalIllumination.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/vec4.hpp>
#include <shaders/GiFill.cs.spv.h>
#include <shaders/GiReflect.cs.spv.h>
#include <shaders/GiResolve.cs.spv.h>
#include <shaders/GiTrace.cs.spv.h>
#include <shaders/GiUpdate.cs.spv.h>

#include <algorithm>
#include <array>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

// Matches GiPushConstants in shaders/GlobalIllumination.slangh.
struct GiPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t keys;
    std::uint32_t accumulation;
    std::uint32_t radiance;
    std::uint32_t ages;
    std::uint32_t anchors;
    std::uint32_t depth;
    std::uint32_t normals;
    std::uint32_t output;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t seed;
    std::uint32_t mode;
    std::uint32_t spacing;
    std::uint32_t reset;
    std::uint32_t reflections;
    float maxRoughness;
    std::uint32_t counts;
    float clamp;
    std::uint32_t history;
    std::uint32_t previousHistory;
    std::uint32_t historyValid;
};
static_assert(sizeof(GiPushConstants) <= rhi::kMaxPushConstantSize);

namespace {

// The settings' counts, a byte each, as the shaders read them (GiPushConstants::counts).
std::uint32_t Counts(const GlobalIlluminationSettings& settings) {
    const std::uint32_t bounces = std::min(settings.bounces, 3U);
    const std::uint32_t candidates = std::clamp(settings.lightCandidates, 1U, 8U);
    const std::uint32_t rays = std::clamp(settings.rays, 1U, 16U);
    return bounces | (candidates << 8U) | (rays << 16U);
}

constexpr std::uint32_t kGroupSize = 8;
constexpr std::uint32_t kResolveGroupSize = 64;

} // namespace

GlobalIllumination::GlobalIllumination(GlobalIllumination&& other) noexcept
    : m_device(other.m_device), m_update(std::exchange(other.m_update, {})),
      m_resolve(std::exchange(other.m_resolve, {})), m_trace(std::exchange(other.m_trace, {})),
      m_fill(std::exchange(other.m_fill, {})), m_reflect(std::exchange(other.m_reflect, {})),
      m_keys(std::exchange(other.m_keys, {})), m_accumulation(std::exchange(other.m_accumulation, {})),
      m_radiance(std::exchange(other.m_radiance, {})), m_ages(std::exchange(other.m_ages, {})),
      m_anchors(std::exchange(other.m_anchors, {})), m_output(std::exchange(other.m_output, {})),
      m_reflections(std::exchange(other.m_reflections, {})),
      m_history{std::exchange(other.m_history[0], {}), std::exchange(other.m_history[1], {})}, m_extent(other.m_extent),
      m_reset(other.m_reset), m_historyFrame(other.m_historyFrame) {}

GlobalIllumination::~GlobalIllumination() {
    m_device->DestroyPipeline(m_update);
    m_device->DestroyPipeline(m_resolve);
    m_device->DestroyPipeline(m_trace);
    m_device->DestroyPipeline(m_fill);
    m_device->DestroyPipeline(m_reflect);
    m_device->DestroyBuffer(m_keys);
    m_device->DestroyBuffer(m_accumulation);
    m_device->DestroyBuffer(m_radiance);
    m_device->DestroyBuffer(m_ages);
    m_device->DestroyBuffer(m_anchors);
    m_device->DestroyTexture(m_output);
    m_device->DestroyTexture(m_reflections);
    m_device->DestroyTexture(m_history[0]);
    m_device->DestroyTexture(m_history[1]);
}

core::Result<GlobalIllumination> GlobalIllumination::Create(rhi::Device& device) {
    GlobalIllumination gi(device);
    DAI_TRY_ASSIGN(gi.m_update, device.CreateComputePipeline(
                                    {.shader = rhi::ToSpirv(kGiUpdateCsSpirv), .name = "Radiance cache update"}));
    DAI_TRY_ASSIGN(gi.m_resolve, device.CreateComputePipeline(
                                     {.shader = rhi::ToSpirv(kGiResolveCsSpirv), .name = "Radiance cache resolve"}));
    DAI_TRY_ASSIGN(gi.m_trace,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kGiTraceCsSpirv), .name = "Indirect light"}));
    DAI_TRY_ASSIGN(gi.m_fill, device.CreateComputePipeline(
                                  {.shader = rhi::ToSpirv(kGiFillCsSpirv), .name = "Indirect light fill"}));
    DAI_TRY_ASSIGN(gi.m_reflect,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kGiReflectCsSpirv), .name = "Reflections"}));
    const auto buffer = [&device](rhi::BufferHandle& handle, std::uint64_t elementSize,
                                  const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(handle, device.CreateBuffer({.size = std::uint64_t{kCacheCapacity} * elementSize,
                                                    .usage = rhi::BufferUsage::Storage,
                                                    .name = name}));
        return {};
    };
    DAI_TRY(buffer(gi.m_keys, sizeof(std::uint32_t), "Radiance cache keys"));
    DAI_TRY(buffer(gi.m_accumulation, 4 * sizeof(std::uint32_t), "Radiance cache accumulation"));
    DAI_TRY(buffer(gi.m_radiance, sizeof(glm::vec4), "Radiance cache"));
    DAI_TRY(buffer(gi.m_ages, sizeof(std::uint32_t), "Radiance cache ages"));
    DAI_TRY(buffer(gi.m_anchors, sizeof(glm::vec4), "Radiance cache anchors"));
    return gi;
}

core::Result<void> GlobalIllumination::Resize(rhi::Extent extent) {
    if (m_output && extent == m_extent) {
        return {};
    }
    const auto create = [&](const char* name) {
        return m_device->CreateTexture(
            {.extent = extent,
             .format = kOutputFormat,
             .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
             .name = name});
    };
    std::array<rhi::TextureHandle, 4> textures{};
    const std::array<const char*, 4> names{"Indirect light", "Reflections", "Indirect light history",
                                           "Indirect light history"};
    for (std::size_t index = 0; index < textures.size(); ++index) {
        core::Result<rhi::TextureHandle> texture = create(names[index]);
        if (!texture) {
            for (const rhi::TextureHandle made : textures) {
                m_device->DestroyTexture(made);
            }
            return tl::unexpected(std::move(texture.error()));
        }
        textures[index] = *texture;
    }
    m_device->DestroyTexture(m_output);
    m_device->DestroyTexture(m_reflections);
    m_device->DestroyTexture(m_history[0]);
    m_device->DestroyTexture(m_history[1]);
    m_output = textures[0];
    m_reflections = textures[1];
    m_history = {textures[2], textures[3]};
    m_extent = extent;
    m_historyFrame.reset();
    return {};
}

GiPushConstants GlobalIllumination::PushConstants(const GlobalIlluminationInputs& inputs, Mode mode) const {
    const rhi::Extent extent = inputs.extent.width > 0 ? inputs.extent : m_extent;
    return GiPushConstants{inputs.frameBuffer,
                         inputs.instanceBuffer,
                         inputs.materialBuffer,
                         m_device->StorageIndex(m_keys),
                         m_device->StorageIndex(m_accumulation),
                         m_device->StorageIndex(m_radiance),
                         m_device->StorageIndex(m_ages),
                         m_device->StorageIndex(m_anchors),
                         m_device->BindlessIndex(inputs.depth),
                         m_device->BindlessIndex(inputs.normals),
                         m_device->StorageImageIndex(m_output),
                         extent.width,
                         extent.height,
                         inputs.frameIndex,
                         static_cast<std::uint32_t>(mode),
                         kPathSpacing,
                         0,
                         m_device->StorageImageIndex(m_reflections),
                         inputs.reflectionRoughness,
                         Counts(inputs.settings),
                         inputs.settings.clamp,
                         0,
                         0,
                         0};
}

void GlobalIllumination::AddCacheUpdate(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode) {
    AddCache(graph, inputs, mode, true);
}

void GlobalIllumination::AddCache(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode, bool async) {
    const rhi::Extent extent = inputs.extent.width > 0 ? inputs.extent : m_extent;
    const GiPushConstants push = PushConstants(inputs, mode);
    const std::vector<rhi::BufferUse> cache{{m_keys, rhi::Access::ComputeShaderWrite},
                                            {m_accumulation, rhi::Access::ComputeShaderWrite},
                                            {m_radiance, rhi::Access::ComputeShaderWrite},
                                            {m_ages, rhi::Access::ComputeShaderWrite},
                                            {m_anchors, rhi::Access::ComputeShaderWrite}};
    const std::uint32_t resolveGroups = kCacheCapacity / kResolveGroupSize;
    if (m_reset) {
        GiPushConstants reset = push;
        reset.reset = 1;
        graph.AddPass({.name = "Radiance cache reset", .group = "Radiance cache", .buffers = cache, .async = async},
                      [this, reset, resolveGroups](rhi::CommandList& commands) {
                          commands.BindPipeline(m_resolve);
                          commands.Push(reset);
                          commands.Dispatch(resolveGroups);
                      });
        m_reset = false;
    }

    std::vector<rhi::BufferUse> traced = cache;
    traced.push_back({inputs.skinnedVertices, rhi::Access::ComputeShaderRead});
    traced.push_back({inputs.emissiveTriangles, rhi::Access::ComputeShaderRead});
    traced.push_back({inputs.materialGrid, rhi::Access::ComputeShaderRead});
    const std::uint32_t tilesX = (extent.width + kPathSpacing - 1) / kPathSpacing;
    const std::uint32_t tilesY = (extent.height + kPathSpacing - 1) / kPathSpacing;
    graph.AddPass({.name = "Radiance cache update",
                   .group = "Radiance cache",
                   .buffers = traced,
                   .structures = {{inputs.structure, rhi::Access::RayQueryRead}},
                   .async = async},
                  [this, push, tilesX, tilesY](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Update");
                      commands.BindPipeline(m_update);
                      commands.Push(push);
                      commands.Dispatch((tilesX + kGroupSize - 1) / kGroupSize, (tilesY + kGroupSize - 1) / kGroupSize);
                  });
    graph.AddPass({.name = "Radiance cache resolve", .group = "Radiance cache", .buffers = cache, .async = async},
                  [this, push, resolveGroups](rhi::CommandList& commands) {
                      commands.BindPipeline(m_resolve);
                      commands.Push(push);
                      commands.Dispatch(resolveGroups);
                  });
    m_cacheAdded = true;
}

void GlobalIllumination::AddTo(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode) {
    // The cache's passes, unless AddCacheUpdate added them earlier this frame.
    if (!std::exchange(m_cacheAdded, false)) {
        AddCache(graph, inputs, mode, false);
        m_cacheAdded = false;
    }
    const rhi::Extent extent = inputs.extent.width > 0 ? inputs.extent : m_extent;
    const std::uint32_t groupsX = (extent.width + kGroupSize - 1) / kGroupSize;
    const std::uint32_t groupsY = (extent.height + kGroupSize - 1) / kGroupSize;
    const GiPushConstants push = PushConstants(inputs, mode);
    // What the rays read: the cache, and the frame's vertices, emissive triangles and material grid.
    const std::vector<rhi::BufferUse> traced{{m_keys, rhi::Access::ComputeShaderWrite},
                                             {m_accumulation, rhi::Access::ComputeShaderWrite},
                                             {m_radiance, rhi::Access::ComputeShaderWrite},
                                             {m_ages, rhi::Access::ComputeShaderWrite},
                                             {m_anchors, rhi::Access::ComputeShaderWrite},
                                             {inputs.skinnedVertices, rhi::Access::ComputeShaderRead},
                                             {inputs.emissiveTriangles, rhi::Access::ComputeShaderRead},
                                             {inputs.materialGrid, rhi::Access::ComputeShaderRead}};
    // Half the pixels trace, or a quarter, and the upsample fills the others (ADR-0093). The cache's debug view
    // shows every pixel's own cell.
    const std::uint32_t perRay = mode == Mode::Indirect ? inputs.settings.pixelsPerRay : 1;
    const bool sparse = perRay == 2 || perRay == 4;
    GiPushConstants trace = push;
    trace.spacing = sparse ? perRay : 1;
    const std::uint32_t traceGroupsX = sparse ? (groupsX + 1) / 2 : groupsX;
    const std::uint32_t traceGroupsY = perRay == 4 ? (groupsY + 1) / 2 : groupsY;
    graph.AddPass({.name = "Indirect light",
                   .group = "Indirect light",
                   .textures = {{m_output, rhi::Access::ComputeShaderWrite, !sparse},
                                {inputs.depth, rhi::Access::ComputeShaderRead},
                                {inputs.normals, rhi::Access::ComputeShaderRead}},
                   .buffers = traced,
                   .structures = {{inputs.structure, rhi::Access::RayQueryRead}}},
                  [this, trace, traceGroupsX, traceGroupsY](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Trace");
                      commands.BindPipeline(m_trace);
                      commands.Push(trace);
                      commands.Dispatch(traceGroupsX, traceGroupsY);
                  });
    if (sparse) {
        // The history alternates between two textures: the fill reads last frame's while it writes this one's.
        const std::size_t current = inputs.frameIndex % 2;
        GiPushConstants fill = trace;
        fill.history = m_device->StorageImageIndex(m_history[current]);
        fill.previousHistory = m_device->StorageImageIndex(m_history[1 - current]);
        fill.historyValid = m_historyFrame.has_value() && *m_historyFrame + 1 == inputs.frameIndex ? 1 : 0;
        m_historyFrame = inputs.frameIndex;
        graph.AddPass({.name = "Indirect light fill",
                       .group = "Indirect light",
                       .textures = {{m_output, rhi::Access::ComputeShaderWrite},
                                    {m_history[current], rhi::Access::ComputeShaderWrite, true},
                                    {m_history[1 - current], rhi::Access::ComputeShaderRead},
                                    {inputs.depth, rhi::Access::ComputeShaderRead},
                                    {inputs.normals, rhi::Access::ComputeShaderRead}}},
                      [this, fill, groupsX, groupsY](rhi::CommandList& commands) {
                          DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Fill");
                          commands.BindPipeline(m_fill);
                          commands.Push(fill);
                          commands.Dispatch(groupsX, groupsY);
                      });
    } else {
        m_historyFrame.reset();
    }
    if (!inputs.reflections || mode != Mode::Indirect) {
        return;
    }
    graph.AddPass({.name = "Reflections",
                   .group = "Reflections",
                   .textures = {{m_reflections, rhi::Access::ComputeShaderWrite, true},
                                {m_output, rhi::Access::ComputeShaderWrite},
                                {inputs.depth, rhi::Access::ComputeShaderRead},
                                {inputs.normals, rhi::Access::ComputeShaderRead}},
                   .buffers = traced,
                   .structures = {{inputs.structure, rhi::Access::RayQueryRead}}},
                  [this, push, groupsX, groupsY](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Reflect");
                      commands.BindPipeline(m_reflect);
                      commands.Push(push);
                      commands.Dispatch(groupsX, groupsY);
                  });
}

} // namespace daidalos::renderer
