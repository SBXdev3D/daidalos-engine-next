// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "AmbientOcclusionPass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/vec2.hpp>
#include <shaders/Ssao.ps.spv.h>
#include <shaders/Ssao.vs.spv.h>
#include <shaders/SsaoBlur.ps.spv.h>
#include <shaders/SsaoBlur.vs.spv.h>

#include <algorithm>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches AmbientOcclusionPushConstants in shaders/AmbientOcclusion.slangh.
struct AmbientOcclusionPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t depthTexture;
    std::uint32_t normalTexture;
    std::uint32_t sourceTexture;
    float radius;
    float intensity;
    glm::vec2 sourceTexel;
};

/// Meters around a pixel where geometry occludes it: the size of the creases that darken.
constexpr float kRadius = 0.5f;
constexpr float kIntensity = 1.5f;

glm::vec2 Texel(rhi::Extent extent) {
    return {1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height)};
}

} // namespace

AmbientOcclusionPass::AmbientOcclusionPass(AmbientOcclusionPass&& other) noexcept
    : m_device(other.m_device), m_occlusionPipeline(std::exchange(other.m_occlusionPipeline, {})),
      m_blurPipeline(std::exchange(other.m_blurPipeline, {})), m_raw(std::exchange(other.m_raw, {})),
      m_blurred(std::exchange(other.m_blurred, {})), m_frameExtent(other.m_frameExtent), m_extent(other.m_extent) {}

AmbientOcclusionPass::~AmbientOcclusionPass() {
    m_device->DestroyPipeline(m_occlusionPipeline);
    m_device->DestroyPipeline(m_blurPipeline);
    m_device->DestroyTexture(m_raw);
    m_device->DestroyTexture(m_blurred);
}

core::Result<AmbientOcclusionPass> AmbientOcclusionPass::Create(rhi::Device& device) {
    AmbientOcclusionPass pass(device);
    DAI_TRY_ASSIGN(pass.m_occlusionPipeline, device.CreatePipeline({
                                                 .vertexShader = rhi::ToSpirv(kSsaoVsSpirv),
                                                 .fragmentShader = rhi::ToSpirv(kSsaoPsSpirv),
                                                 .colorFormat = rhi::Format::R8Unorm,
                                                 .cull = rhi::CullMode::None,
                                                 .name = "Ambient occlusion",
                                             }));
    DAI_TRY_ASSIGN(pass.m_blurPipeline, device.CreatePipeline({
                                            .vertexShader = rhi::ToSpirv(kSsaoBlurVsSpirv),
                                            .fragmentShader = rhi::ToSpirv(kSsaoBlurPsSpirv),
                                            .colorFormat = rhi::Format::R8Unorm,
                                            .cull = rhi::CullMode::None,
                                            .name = "Ambient occlusion blur",
                                        }));
    return pass;
}

core::Result<void> AmbientOcclusionPass::Resize(rhi::Extent frame) {
    if (m_raw && frame == m_frameExtent) {
        return {};
    }
    m_device->DestroyTexture(m_raw);
    m_device->DestroyTexture(m_blurred);
    m_raw = {};
    m_blurred = {};
    const rhi::Extent half{std::max(frame.width / 2, 1U), std::max(frame.height / 2, 1U)};
    const rhi::TextureDesc desc{.extent = half,
                                .format = rhi::Format::R8Unorm,
                                // TransferSrc lets tests and captures read it back.
                                .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled |
                                         rhi::TextureUsage::TransferSrc,
                                .name = "Ambient occlusion"};
    DAI_TRY_ASSIGN(m_raw, m_device->CreateTexture(desc));
    DAI_TRY_ASSIGN(m_blurred, m_device->CreateTexture(desc));
    m_frameExtent = frame;
    m_extent = half;
    return {};
}

void AmbientOcclusionPass::AddTo(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle depth,
                                 rhi::TextureHandle normals) {
    const AmbientOcclusionPushConstants push{
        frameBuffer,         m_device->BindlessIndex(depth), m_device->BindlessIndex(normals), 0, kRadius, kIntensity,
        Texel(m_frameExtent)};
    graph.AddPass({.name = "Ambient occlusion",
                   .group = "Occlusion",
                   .rendering = rhi::RenderingDesc{.colorTarget = m_raw},
                   .textures = {{m_raw, rhi::Access::ColorTarget, true},
                                {depth, rhi::Access::FragmentShaderRead},
                                {normals, rhi::Access::FragmentShaderRead}}},
                  [this, push](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::AmbientOcclusion");
                      commands.BindPipeline(m_occlusionPipeline);
                      commands.Push(push);
                      commands.Draw(3);
                  });

    AmbientOcclusionPushConstants blur = push;
    blur.sourceTexture = m_device->BindlessIndex(m_raw);
    blur.sourceTexel = Texel(m_extent);
    graph.AddPass({.name = "Occlusion blur",
                   .group = "Occlusion",
                   .rendering = rhi::RenderingDesc{.colorTarget = m_blurred},
                   .textures = {{m_blurred, rhi::Access::ColorTarget, true}, {m_raw, rhi::Access::FragmentShaderRead}}},
                  [this, blur](rhi::CommandList& commands) {
                      commands.BindPipeline(m_blurPipeline);
                      commands.Push(blur);
                      commands.Draw(3);
                  });
}

} // namespace daidalos::renderer
