// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "GlobalIllumination.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/vec4.hpp>
#include <shaders/GiReflect.cs.spv.h>
#include <shaders/GiResolve.cs.spv.h>
#include <shaders/GiTrace.cs.spv.h>
#include <shaders/GiUpdate.cs.spv.h>

#include <algorithm>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

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
};
static_assert(sizeof(GiPushConstants) <= rhi::kMaxPushConstantSize);

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
      m_reflect(std::exchange(other.m_reflect, {})), m_keys(std::exchange(other.m_keys, {})),
      m_accumulation(std::exchange(other.m_accumulation, {})), m_radiance(std::exchange(other.m_radiance, {})),
      m_ages(std::exchange(other.m_ages, {})), m_anchors(std::exchange(other.m_anchors, {})),
      m_output(std::exchange(other.m_output, {})), m_reflections(std::exchange(other.m_reflections, {})),
      m_extent(other.m_extent), m_reset(other.m_reset) {}

GlobalIllumination::~GlobalIllumination() {
    m_device->DestroyPipeline(m_update);
    m_device->DestroyPipeline(m_resolve);
    m_device->DestroyPipeline(m_trace);
    m_device->DestroyPipeline(m_reflect);
    m_device->DestroyBuffer(m_keys);
    m_device->DestroyBuffer(m_accumulation);
    m_device->DestroyBuffer(m_radiance);
    m_device->DestroyBuffer(m_ages);
    m_device->DestroyBuffer(m_anchors);
    m_device->DestroyTexture(m_output);
    m_device->DestroyTexture(m_reflections);
}

core::Result<GlobalIllumination> GlobalIllumination::Create(rhi::Device& device) {
    GlobalIllumination gi(device);
    DAI_TRY_ASSIGN(gi.m_update, device.CreateComputePipeline(
                                    {.shader = rhi::ToSpirv(kGiUpdateCsSpirv), .name = "Radiance cache update"}));
    DAI_TRY_ASSIGN(gi.m_resolve, device.CreateComputePipeline(
                                     {.shader = rhi::ToSpirv(kGiResolveCsSpirv), .name = "Radiance cache resolve"}));
    DAI_TRY_ASSIGN(gi.m_trace,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kGiTraceCsSpirv), .name = "Indirect light"}));
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
    DAI_TRY_ASSIGN(rhi::TextureHandle output, create("Indirect light"));
    core::Result<rhi::TextureHandle> reflections = create("Reflections");
    if (!reflections) {
        m_device->DestroyTexture(output);
        return tl::unexpected(std::move(reflections.error()));
    }
    m_device->DestroyTexture(m_output);
    m_device->DestroyTexture(m_reflections);
    m_output = output;
    m_reflections = *reflections;
    m_extent = extent;
    return {};
}

void GlobalIllumination::AddTo(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode) {
    GiPushConstants push{inputs.frameBuffer,
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
                         m_extent.width,
                         m_extent.height,
                         inputs.frameIndex,
                         static_cast<std::uint32_t>(mode),
                         kPathSpacing,
                         0,
                         m_device->StorageImageIndex(m_reflections),
                         inputs.reflectionRoughness,
                         Counts(inputs.settings),
                         inputs.settings.clamp};
    const std::vector<rhi::BufferUse> cache{{m_keys, rhi::Access::ComputeShaderWrite},
                                            {m_accumulation, rhi::Access::ComputeShaderWrite},
                                            {m_radiance, rhi::Access::ComputeShaderWrite},
                                            {m_ages, rhi::Access::ComputeShaderWrite},
                                            {m_anchors, rhi::Access::ComputeShaderWrite}};
    const std::uint32_t resolveGroups = kCacheCapacity / kResolveGroupSize;
    if (m_reset) {
        GiPushConstants reset = push;
        reset.reset = 1;
        graph.AddPass({.name = "Radiance cache reset", .group = "Radiance cache", .buffers = cache},
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
    const std::uint32_t tilesX = (m_extent.width + kPathSpacing - 1) / kPathSpacing;
    const std::uint32_t tilesY = (m_extent.height + kPathSpacing - 1) / kPathSpacing;
    graph.AddPass({.name = "Radiance cache update",
                   .group = "Radiance cache",
                   .buffers = traced,
                   .structures = {{inputs.structure, rhi::Access::RayQueryRead}}},
                  [this, push, tilesX, tilesY](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Update");
                      commands.BindPipeline(m_update);
                      commands.Push(push);
                      commands.Dispatch((tilesX + kGroupSize - 1) / kGroupSize, (tilesY + kGroupSize - 1) / kGroupSize);
                  });
    graph.AddPass({.name = "Radiance cache resolve", .group = "Radiance cache", .buffers = cache},
                  [this, push, resolveGroups](rhi::CommandList& commands) {
                      commands.BindPipeline(m_resolve);
                      commands.Push(push);
                      commands.Dispatch(resolveGroups);
                  });
    graph.AddPass({.name = "Indirect light",
                   .group = "Indirect light",
                   .textures = {{m_output, rhi::Access::ComputeShaderWrite, true},
                                {inputs.depth, rhi::Access::ComputeShaderRead},
                                {inputs.normals, rhi::Access::ComputeShaderRead}},
                   .buffers = traced,
                   .structures = {{inputs.structure, rhi::Access::RayQueryRead}}},
                  [this, push](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Trace");
                      commands.BindPipeline(m_trace);
                      commands.Push(push);
                      commands.Dispatch((m_extent.width + kGroupSize - 1) / kGroupSize,
                                        (m_extent.height + kGroupSize - 1) / kGroupSize);
                  });
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
                  [this, push](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::GlobalIllumination::Reflect");
                      commands.BindPipeline(m_reflect);
                      commands.Push(push);
                      commands.Dispatch((m_extent.width + kGroupSize - 1) / kGroupSize,
                                        (m_extent.height + kGroupSize - 1) / kGroupSize);
                  });
}

} // namespace daidalos::renderer
