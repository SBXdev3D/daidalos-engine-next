// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "FxaaPass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/vec2.hpp>
#include <shaders/Fxaa.ps.spv.h>
#include <shaders/Fxaa.vs.spv.h>

#include <algorithm>
#include <utility>

namespace daidalos::renderer {

namespace {

// Matches FxaaPushConstants in shaders/Fxaa.hlsl.
struct FxaaPushConstants {
    std::uint32_t sourceTexture;
    std::uint32_t padding;
    glm::vec2 texel;
};

} // namespace

core::Result<FxaaPass> FxaaPass::Create(rhi::Device& device, rhi::Format targetFormat) {
    FxaaPass pass(device);
    DAI_TRY_ASSIGN(pass.m_pipeline, device.CreatePipeline({
                                        .vertexShader = rhi::ToSpirv(kFxaaVsSpirv),
                                        .fragmentShader = rhi::ToSpirv(kFxaaPsSpirv),
                                        .colorFormat = targetFormat,
                                        .cull = rhi::CullMode::None,
                                        .name = "FXAA",
                                    }));
    return pass;
}

FxaaPass::FxaaPass(FxaaPass&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})) {}

FxaaPass::~FxaaPass() {
    m_device->DestroyPipeline(m_pipeline);
}

void FxaaPass::Record(rhi::CommandList& commands, rhi::TextureHandle source, rhi::Extent extent) {
    DAI_PROFILE_SCOPE("renderer::FxaaPass::Record");
    commands.BindPipeline(m_pipeline);
    commands.Push(FxaaPushConstants{m_device->BindlessIndex(source), 0,
                                    glm::vec2(1.0f / static_cast<float>(std::max(extent.width, 1U)),
                                              1.0f / static_cast<float>(std::max(extent.height, 1U)))});
    commands.Draw(3);
}

} // namespace daidalos::renderer
