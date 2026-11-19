// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "VolumetricFog.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/FogIntegrate.cs.spv.h>
#include <shaders/FogLighting.cs.spv.h>
#include <shaders/FogSmoke.cs.spv.h>

#include <algorithm>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches FogPushConstants in shaders/FogLighting.slang and shaders/FogIntegrate.slang.
struct FogPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t scattering;
    std::uint32_t history;
    std::uint32_t integrated;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t slices;
    std::uint32_t seed;
    float historyWeight;
    std::uint32_t lightMap;
    std::uint32_t previousLightCount;
    std::uint32_t smoke;
    std::uint32_t smokeList;
    std::uint32_t padding;
};
static_assert(sizeof(FogPushConstants) <= rhi::kMaxPushConstantSize);

constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;
// Match FogSmoke.slang and ParticlePass::kMaxSmokeParticles: a group per particle.
constexpr std::uint32_t kSmokeGroups = 1024;
// Match the thread groups of the shaders.
constexpr std::uint32_t kLightingGroup = 4;
constexpr std::uint32_t kIntegrateGroup = 8;

std::uint32_t Groups(std::uint32_t count, std::uint32_t group) {
    return (count + group - 1) / group;
}

} // namespace

VolumetricFog::VolumetricFog(VolumetricFog&& other) noexcept
    : m_device(other.m_device), m_lighting(std::exchange(other.m_lighting, {})),
      m_integrate(std::exchange(other.m_integrate, {})), m_smokeSplat(std::exchange(other.m_smokeSplat, {})),
      m_smoke(std::exchange(other.m_smoke, {})), m_smokeEmptied(other.m_smokeEmptied),
      m_scattering(std::exchange(other.m_scattering, {})), m_integrated(std::exchange(other.m_integrated, {})),
      m_current(other.m_current), m_output(other.m_output), m_grid(other.m_grid), m_size(other.m_size),
      m_slices(other.m_slices), m_hasHistory(other.m_hasHistory) {}

VolumetricFog::~VolumetricFog() {
    m_device->DestroyPipeline(m_lighting);
    m_device->DestroyPipeline(m_integrate);
    m_device->DestroyPipeline(m_smokeSplat);
    DestroyResources();
}

void VolumetricFog::DestroyResources() {
    for (const rhi::TextureHandle texture : m_scattering) {
        m_device->DestroyTexture(texture);
    }
    m_device->DestroyTexture(m_integrated);
    m_device->DestroyTexture(m_smoke);
    m_scattering = {};
    m_integrated = {};
    m_smoke = {};
    m_smokeEmptied = false;
}

core::Result<VolumetricFog> VolumetricFog::Create(rhi::Device& device) {
    VolumetricFog fog(device);
    DAI_TRY_ASSIGN(fog.m_lighting,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kFogLightingCsSpirv), .name = "Fog lighting"}));
    DAI_TRY_ASSIGN(fog.m_integrate, device.CreateComputePipeline(
                                        {.shader = rhi::ToSpirv(kFogIntegrateCsSpirv), .name = "Fog integration"}));
    DAI_TRY_ASSIGN(fog.m_smokeSplat,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kFogSmokeCsSpirv), .name = "Fog smoke"}));
    return fog;
}

core::Result<void> VolumetricFog::Resize(rhi::Extent output, const FroxelGrid& grid) {
    if (m_integrated && output == m_output && grid == m_grid) {
        return {};
    }
    DestroyResources();
    m_hasHistory = false;
    const std::uint32_t tile = std::max(grid.tileSize, 1U);
    const rhi::Extent size{std::max(Groups(output.width, tile), 1U), std::max(Groups(output.height, tile), 1U)};
    const std::uint32_t slices = std::max(grid.slices, 2U);
    const auto texture = [&](rhi::TextureHandle& created, const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(created,
                       m_device->CreateTexture({.extent = size,
                                                .format = kFormat,
                                                .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled,
                                                .depth = slices,
                                                .name = name}));
        return {};
    };
    core::Result<void> created =
        texture(m_scattering[0], "Fog scattering")

            .and_then([&] { return texture(m_scattering[1], "Fog scattering"); })
            .and_then([&] { return texture(m_integrated, "Fog"); })
            .and_then([&]() -> core::Result<void> {
                DAI_TRY_ASSIGN(m_smoke, m_device->CreateTexture({.extent = size,
                                                                 .format = kSmokeFormat,
                                                                 .usage = rhi::TextureUsage::Storage,
                                                                 .depth = slices,
                                                                 .name = "Fog smoke"}));
                return {};
            });
    if (!created) {
        DestroyResources();
        return created;
    }
    m_output = output;
    m_grid = grid;
    m_size = size;
    m_slices = slices;
    return {};
}

void VolumetricFog::AddTo(RenderGraph& graph, const VolumetricFogInputs& inputs) {
    const std::uint32_t previous = m_current;
    m_current = 1 - m_current;
    const FogPushConstants push{inputs.frameBuffer,
                                inputs.instanceBuffer,
                                inputs.materialBuffer,
                                m_device->StorageImageIndex(m_scattering[m_current]),
                                m_hasHistory ? m_device->BindlessIndex(m_scattering[previous]) : kNoTexture,
                                m_device->StorageImageIndex(m_integrated),
                                m_size.width,
                                m_size.height,
                                m_slices,
                                inputs.frameIndex,
                                kHistoryWeight,
                                inputs.lightMap,
                                inputs.previousLightCount,
                                m_smokeEmptied ? m_device->StorageImageIndex(m_smoke) : kNoTexture,
                                inputs.smoke ? m_device->StorageIndex(inputs.smoke) : kNoTexture,
                                0};
    if (inputs.smoke && m_smokeEmptied) {
        // Smoke of the particles (ADR-0062), into the froxels the lighting reads.
        graph.AddPass({.name = "Fog smoke",
                       .group = "Volumetric fog",
                       .textures = {{m_smoke, rhi::Access::ComputeShaderWrite}},
                       .buffers = {{inputs.smoke, rhi::Access::ComputeShaderRead}},
                       .async = inputs.async},
                      [this, push](rhi::CommandList& commands) {
                          commands.BindPipeline(m_smokeSplat);
                          commands.Push(push);
                          commands.Dispatch(kSmokeGroups);
                      });
    }
    PassDesc lighting{.name = "Fog lighting",
                      .group = "Volumetric fog",
                      .textures = {{m_scattering[m_current], rhi::Access::ComputeShaderWrite, true},
                                   // Read as a storage image.
                                   {m_smoke, rhi::Access::ComputeShaderWrite}},
                      .buffers = {{inputs.skinnedVertices, rhi::Access::ComputeShaderRead},
                                  {inputs.emissiveTriangles, rhi::Access::ComputeShaderRead}},
                      .structures = {{inputs.structure, rhi::Access::RayQueryRead}},
                      .async = inputs.async};
    if (m_hasHistory) {
        lighting.textures.push_back({m_scattering[previous], rhi::Access::ComputeShaderRead});
    }
    graph.AddPass(std::move(lighting), [this, push](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::VolumetricFog");
        commands.BindPipeline(m_lighting);
        commands.Push(push);
        commands.Dispatch(Groups(m_size.width, kLightingGroup), Groups(m_size.height, kLightingGroup),
                          Groups(m_slices, kLightingGroup));
    });

    FogPushConstants integratePush = push;
    integratePush.scattering = m_device->BindlessIndex(m_scattering[m_current]);
    // The integration empties the smoke for the next frame.
    integratePush.smoke = m_device->StorageImageIndex(m_smoke);
    graph.AddPass({.name = "Fog integration",
                   .group = "Volumetric fog",
                   .textures = {{m_scattering[m_current], rhi::Access::ComputeShaderRead},
                                {m_integrated, rhi::Access::ComputeShaderWrite, true},
                                {m_smoke, rhi::Access::ComputeShaderWrite, true}},
                   .async = inputs.async},
                  [this, integratePush](rhi::CommandList& commands) {
                      commands.BindPipeline(m_integrate);
                      commands.Push(integratePush);
                      commands.Dispatch(Groups(m_size.width, kIntegrateGroup), Groups(m_size.height, kIntegrateGroup));
                  });
    m_hasHistory = true;
    m_smokeEmptied = true;
}

} // namespace daidalos::renderer
