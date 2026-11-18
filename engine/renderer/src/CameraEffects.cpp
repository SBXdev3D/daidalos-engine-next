// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "CameraEffects.hpp"

#include <daidalos/rhi/Spirv.hpp>

#include <shaders/CameraEffects.cs.spv.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches CameraEffectsPushConstants in shaders/CameraEffects.slang.
struct CameraEffectsPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t color;
    std::uint32_t depth;
    std::uint32_t motion;
    std::uint32_t output;
    std::uint32_t mode;
    std::uint32_t width;
    std::uint32_t height;
    float cocScale;
    float focusDistance;
    float maxCoc;
    float shutter;
};

constexpr std::uint32_t kDepthOfField = 0;
constexpr std::uint32_t kMotionBlur = 1;
constexpr std::uint32_t kGroupSize = 8;
// A full-frame sensor's height, in meters.
constexpr float kSensorHeight = 0.024f;

} // namespace

float CircleOfConfusionScale(float aperture, float focusDistance, float verticalFov, std::uint32_t imageHeight) {
    if (aperture <= 0.0f || focusDistance <= 0.0f) {
        return 0.0f;
    }
    const float focalLength = 0.5f * kSensorHeight / std::tan(0.5f * std::max(verticalFov, 0.01f));
    const float focus = std::max(focusDistance, focalLength * 1.01f);
    // The thin lens: the blur on the sensor is f² / (N (s - f)) times |d - s| / d.
    const float onSensor = focalLength * focalLength / (aperture * (focus - focalLength));
    return onSensor / kSensorHeight * static_cast<float>(imageHeight);
}

CameraEffects::CameraEffects(CameraEffects&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_images(std::exchange(other.m_images, {})), m_extent(other.m_extent) {}

CameraEffects::~CameraEffects() {
    m_device->DestroyPipeline(m_pipeline);
    DestroyImages();
}

void CameraEffects::DestroyImages() {
    for (rhi::TextureHandle& image : m_images) {
        m_device->DestroyTexture(image);
        image = {};
    }
}

core::Result<CameraEffects> CameraEffects::Create(rhi::Device& device) {
    CameraEffects effects(device);
    DAI_TRY_ASSIGN(effects.m_pipeline, device.CreateComputePipeline(
                                           {.shader = rhi::ToSpirv(kCameraEffectsCsSpirv), .name = "Camera effects"}));
    return effects;
}

core::Result<void> CameraEffects::Resize(rhi::Extent extent) {
    if (m_images[0] && extent == m_extent) {
        return {};
    }
    DestroyImages();
    m_extent = extent;
    for (rhi::TextureHandle& image : m_images) {
        DAI_TRY_ASSIGN(image,
                       m_device->CreateTexture({.extent = extent,
                                                .format = rhi::Format::Rgba16Float,
                                                .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled |
                                                         rhi::TextureUsage::TransferSrc,
                                                .name = "Camera effects"}));
    }
    return {};
}

rhi::TextureHandle CameraEffects::AddTo(RenderGraph& graph, const CameraEffectsInputs& inputs) {
    const float cocScale =
        CircleOfConfusionScale(inputs.aperture, inputs.focusDistance, inputs.verticalFov, m_extent.height);
    rhi::TextureHandle image = inputs.color;
    std::uint32_t next = 0;
    const auto add = [&](std::uint32_t mode, const char* name) {
        const rhi::TextureHandle output = m_images[next];
        next = 1 - next;
        const CameraEffectsPushConstants push{.frameBuffer = inputs.frameBuffer,
                                              .color = m_device->BindlessIndex(image),
                                              .depth = m_device->BindlessIndex(inputs.depth),
                                              .motion = m_device->BindlessIndex(inputs.motion),
                                              .output = m_device->StorageImageIndex(output),
                                              .mode = mode,
                                              .width = m_extent.width,
                                              .height = m_extent.height,
                                              .cocScale = cocScale,
                                              .focusDistance = inputs.focusDistance,
                                              .maxCoc = kMaxCoc,
                                              .shutter = std::clamp(inputs.shutter, 0.0f, 1.0f)};
        graph.AddPass({.name = name,
                       .group = "Camera effects",
                       .textures = {{output, rhi::Access::ComputeShaderWrite, true},
                                    {image, rhi::Access::ComputeShaderRead},
                                    {inputs.depth, rhi::Access::ComputeShaderRead},
                                    {inputs.motion, rhi::Access::ComputeShaderRead}}},
                      [this, push](rhi::CommandList& commands) {
                          commands.BindPipeline(m_pipeline);
                          commands.Push(push);
                          commands.Dispatch((push.width + kGroupSize - 1) / kGroupSize,
                                            (push.height + kGroupSize - 1) / kGroupSize);
                      });
        image = output;
    };
    if (cocScale > 0.0f) {
        add(kDepthOfField, "Depth of field");
    }
    if (inputs.shutter > 0.0f) {
        add(kMotionBlur, "Motion blur");
    }
    return image;
}

} // namespace daidalos::renderer
