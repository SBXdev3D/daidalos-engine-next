// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "PathTracerPass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/PathTracer.cs.spv.h>

#include <array>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches PathTracerPushConstants in shaders/PathTracer.slang.
struct PathTracerPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t output;
    std::uint32_t sampleIndex;
    std::uint32_t maxBounces;
    std::uint32_t seed;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t jitter;
    std::array<std::uint32_t, 2> padding;
    glm::vec3 background;
    float padding2;
};
static_assert(sizeof(PathTracerPushConstants) == 64);

constexpr std::uint32_t kGroupSize = 8;

} // namespace

PathTracerPass::PathTracerPass(rhi::Device& device) : m_device(&device) {}

PathTracerPass::PathTracerPass(PathTracerPass&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_output(std::exchange(other.m_output, {})), m_extent(other.m_extent), m_samples(other.m_samples) {}

PathTracerPass::~PathTracerPass() {
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyTexture(m_output);
}

core::Result<PathTracerPass> PathTracerPass::Create(rhi::Device& device) {
    PathTracerPass pass(device);
    DAI_TRY_ASSIGN(pass.m_pipeline,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kPathTracerCsSpirv), .name = "Path tracer"}));
    return pass;
}

core::Result<void> PathTracerPass::Resize(rhi::Extent extent) {
    if (m_output && extent == m_extent) {
        return {};
    }
    DAI_TRY_ASSIGN(rhi::TextureHandle output,
                   m_device->CreateTexture({.extent = extent,
                                            .format = rhi::Format::Rgba32Float,
                                            .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled |
                                                     rhi::TextureUsage::TransferSrc,
                                            .name = "Path tracer"}));
    m_device->DestroyTexture(m_output);
    m_output = output;
    m_extent = extent;
    m_samples = 0;
    return {};
}

void PathTracerPass::AddTo(RenderGraph& graph, const PathTracerInputs& inputs, const PathTracerSettings& settings) {
    if (m_samples >= settings.maxSamples) {
        return;
    }
    const PathTracerPushConstants push{inputs.frameBuffer,
                                       inputs.instanceBuffer,
                                       inputs.materialBuffer,
                                       m_device->StorageImageIndex(m_output),
                                       m_samples,
                                       std::max(settings.maxBounces, 1U),
                                       settings.seed,
                                       m_extent.width,
                                       m_extent.height,
                                       settings.jitter ? 1U : 0U,
                                       {},
                                       settings.background,
                                       0.0f};
    // The first path of an average overwrites whatever the image held.
    PassDesc desc{.name = "Path tracer",
                  .textures = {{m_output, rhi::Access::ComputeShaderWrite, m_samples == 0}},
                  .structures = {{inputs.structure, rhi::Access::RayQueryRead}}};
    desc.buffers.push_back({inputs.skinnedVertices, rhi::Access::ComputeShaderRead});
    graph.AddPass(std::move(desc), [this, push](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::PathTracer");
        commands.BindPipeline(m_pipeline);
        commands.Push(push);
        commands.Dispatch((m_extent.width + kGroupSize - 1) / kGroupSize,
                          (m_extent.height + kGroupSize - 1) / kGroupSize);
    });
    ++m_samples;
}

} // namespace daidalos::renderer
