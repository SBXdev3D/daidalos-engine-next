// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Fsr3Upscaler.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/Fsr3Accumulate.cs.spv.h>
#include <shaders/Fsr3LumaInstability.cs.spv.h>
#include <shaders/Fsr3LumaPyramid.cs.spv.h>
#include <shaders/Fsr3PrepareInputs.cs.spv.h>
#include <shaders/Fsr3PrepareReactivity.cs.spv.h>
#include <shaders/Fsr3ShadingChange.cs.spv.h>
#include <shaders/Fsr3ShadingChangePyramid.cs.spv.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches Fsr3Data in shaders/Fsr3.slangh: scalars only, so that C++ and std430 lay it out alike.
struct Fsr3Data {
    std::int32_t renderWidth;
    std::int32_t renderHeight;
    std::int32_t previousRenderWidth;
    std::int32_t previousRenderHeight;
    std::int32_t upscaleWidth;
    std::int32_t upscaleHeight;
    std::int32_t previousUpscaleWidth;
    std::int32_t previousUpscaleHeight;
    std::int32_t maxRenderWidth;
    std::int32_t maxRenderHeight;
    std::int32_t maxUpscaleWidth;
    std::int32_t maxUpscaleHeight;
    std::array<float, 4> deviceToViewDepth;
    float jitterX;
    float jitterY;
    float previousJitterX;
    float previousJitterY;
    float motionVectorScaleX;
    float motionVectorScaleY;
    float downscaleFactorX;
    float downscaleFactorY;
    float motionVectorJitterCancellationX;
    float motionVectorJitterCancellationY;
    float tanHalfFov;
    float jitterSequenceLength;
    float deltaTime;
    float deltaPreExposure;
    float viewSpaceToMetersFactor;
    float frameIndex;
    float velocityFactor;
    float reactivenessScale;
    float shadingChangeScale;
    float accumulationAddedPerFrame;
    float minDisocclusionAccumulation;
    std::uint32_t spdMips;
    std::uint32_t spdWorkGroups;
    std::uint32_t spdWorkGroupOffsetX;
    std::uint32_t spdWorkGroupOffsetY;
    std::uint32_t spdRenderWidth;
    std::uint32_t spdRenderHeight;

    std::uint32_t colorTexture;
    std::uint32_t depthTexture;
    std::uint32_t motionTexture;
    std::uint32_t reactiveTexture;
    std::uint32_t dilatedMotionTexture;
    std::uint32_t dilatedMotionImage;
    std::uint32_t dilatedDepthTexture;
    std::uint32_t dilatedDepthImage;
    std::uint32_t reconstructedDepthImage;
    std::uint32_t farthestDepthTexture;
    std::uint32_t farthestDepthImage;
    std::uint32_t farthestDepthMip1Texture;
    std::uint32_t farthestDepthMip1Image;
    std::uint32_t currentLumaTexture;
    std::uint32_t currentLumaImage;
    std::uint32_t previousLumaTexture;
    std::uint32_t lumaHistoryTexture;
    std::uint32_t lumaHistoryImage;
    std::uint32_t lumaInstabilityTexture;
    std::uint32_t lumaInstabilityImage;
    std::uint32_t shadingChangeTexture;
    std::uint32_t shadingChangeImage;
    std::array<std::uint32_t, 6> spdMipTextures;
    std::array<std::uint32_t, 6> spdMipImages;
    std::uint32_t spdMipWidth;
    std::uint32_t spdMipHeight;
    std::uint32_t spdAtomicImage;
    std::uint32_t frameInfoImage;
    std::uint32_t accumulationTexture;
    std::uint32_t accumulationImage;
    std::uint32_t newLocksTexture;
    std::uint32_t newLocksImage;
    std::uint32_t dilatedReactiveTexture;
    std::uint32_t dilatedReactiveImage;
    std::uint32_t historyTexture;
    std::uint32_t historyImage;
    std::uint32_t outputImage;
};
static_assert(sizeof(Fsr3Data) == 90 * 4, "Fsr3Data must match shaders/Fsr3.slangh");

struct Fsr3PushConstants {
    std::uint32_t data;
};

// The passes, in the order they run (AMD's fsr3upscalerDispatch).
enum Pass : std::uint8_t {
    PrepareInputs,
    LumaPyramid,
    ShadingChangePyramid,
    ShadingChange,
    PrepareReactivity,
    LumaInstability,
    Accumulate,
};

// AMD's defaults (fsr3upscalerCreate).
constexpr float kAccumulationAddedPerFrame = 1.0f / 3.0f;
constexpr float kMinDisocclusionAccumulation = -1.0f / 3.0f;
constexpr std::uint32_t kGroupSize = 8;
constexpr std::uint32_t kSpdTile = 64;

std::uint32_t Groups(std::uint32_t size) {
    return (size + kGroupSize - 1) / kGroupSize;
}

} // namespace

Fsr3Upscaler::Fsr3Upscaler(rhi::Device& device) : m_device(&device) {}

Fsr3Upscaler::Fsr3Upscaler(Fsr3Upscaler&& other) noexcept
    : m_device(other.m_device), m_pipelines(std::exchange(other.m_pipelines, {})),
      m_data(std::exchange(other.m_data, {})), m_dataSlot(other.m_dataSlot), m_dilatedMotion(other.m_dilatedMotion),
      m_dilatedDepth(other.m_dilatedDepth), m_reconstructedDepth(other.m_reconstructedDepth),
      m_farthestDepth(other.m_farthestDepth), m_farthestDepthMip1(other.m_farthestDepthMip1), m_luma(other.m_luma),
      m_lumaHistory(other.m_lumaHistory), m_lumaInstability(other.m_lumaInstability),
      m_shadingChange(other.m_shadingChange), m_spdMips(other.m_spdMips), m_spdAtomic(other.m_spdAtomic),
      m_frameInfo(other.m_frameInfo), m_accumulation(other.m_accumulation), m_newLocks(other.m_newLocks),
      m_dilatedReactive(other.m_dilatedReactive), m_history(other.m_history), m_output(other.m_output),
      m_targets(std::exchange(other.m_targets, {})), m_extent(other.m_extent), m_current(other.m_current),
      m_reset(other.m_reset), m_fresh(other.m_fresh), m_previousRenderExtent(other.m_previousRenderExtent),
      m_previousJitter(other.m_previousJitter), m_jitterSequenceLength(other.m_jitterSequenceLength),
      m_frameIndex(other.m_frameIndex) {}

Fsr3Upscaler::~Fsr3Upscaler() {
    DestroyTargets();
    for (const rhi::PipelineHandle pipeline : m_pipelines) {
        m_device->DestroyPipeline(pipeline);
    }
    for (const rhi::BufferHandle buffer : m_data) {
        m_device->DestroyBuffer(buffer);
    }
}

core::Result<Fsr3Upscaler> Fsr3Upscaler::Create(rhi::Device& device) {
    Fsr3Upscaler upscaler(device);
    const std::array<std::pair<std::vector<std::uint32_t>, const char*>, 7> shaders{{
        {rhi::ToSpirv(kFsr3PrepareInputsCsSpirv), "FSR 3.1 prepare inputs"},
        {rhi::ToSpirv(kFsr3LumaPyramidCsSpirv), "FSR 3.1 luma pyramid"},
        {rhi::ToSpirv(kFsr3ShadingChangePyramidCsSpirv), "FSR 3.1 shading change pyramid"},
        {rhi::ToSpirv(kFsr3ShadingChangeCsSpirv), "FSR 3.1 shading change"},
        {rhi::ToSpirv(kFsr3PrepareReactivityCsSpirv), "FSR 3.1 prepare reactivity"},
        {rhi::ToSpirv(kFsr3LumaInstabilityCsSpirv), "FSR 3.1 luma instability"},
        {rhi::ToSpirv(kFsr3AccumulateCsSpirv), "FSR 3.1 accumulate"},
    }};
    for (std::size_t index = 0; index < shaders.size(); ++index) {
        DAI_TRY_ASSIGN(upscaler.m_pipelines[index],
                       device.CreateComputePipeline({.shader = shaders[index].first, .name = shaders[index].second}));
    }
    for (rhi::BufferHandle& buffer : upscaler.m_data) {
        DAI_TRY_ASSIGN(buffer, device.CreateBuffer({.size = sizeof(Fsr3Data),
                                                    .usage = rhi::BufferUsage::Storage,
                                                    .memory = rhi::MemoryLocation::CpuToGpu,
                                                    .name = "FSR 3.1 constants"}));
    }
    return upscaler;
}

void Fsr3Upscaler::DestroyTargets() {
    for (const rhi::TextureHandle texture : m_targets) {
        m_device->DestroyTexture(texture);
    }
    m_targets.clear();
}

core::Result<rhi::TextureHandle> Fsr3Upscaler::CreateTarget(rhi::Extent extent, rhi::Format format, const char* name) {
    // Every target is cleared as a color target, read sampled and written as a storage image.
    DAI_TRY_ASSIGN(const rhi::TextureHandle texture,
                   m_device->CreateTexture({.extent = {std::max(extent.width, 1U), std::max(extent.height, 1U)},
                                            .format = format,
                                            .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled |
                                                     rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                            .name = name}));
    m_targets.push_back(texture);
    return texture;
}

core::Result<void> Fsr3Upscaler::Resize(rhi::Extent extent) {
    if (!m_targets.empty() && extent == m_extent) {
        return {};
    }
    DestroyTargets();
    // The render size goes up to the output's, so FSR's maximum render size is the output's (AMD's
    // maxRenderSize), and the frame is in its top-left corner.
    const rhi::Extent half{extent.width / 2, extent.height / 2};
    using rhi::Format;
    DAI_TRY_ASSIGN(m_dilatedMotion, CreateTarget(extent, Format::Rg16Float, "FSR 3.1 dilated motion"));
    DAI_TRY_ASSIGN(m_dilatedDepth, CreateTarget(extent, Format::R32Float, "FSR 3.1 dilated depth"));
    DAI_TRY_ASSIGN(m_reconstructedDepth, CreateTarget(extent, Format::R32Uint, "FSR 3.1 reconstructed previous depth"));
    DAI_TRY_ASSIGN(m_farthestDepth, CreateTarget(extent, Format::R16Float, "FSR 3.1 farthest depth"));
    DAI_TRY_ASSIGN(m_farthestDepthMip1, CreateTarget(half, Format::R16Float, "FSR 3.1 farthest depth mip 1"));
    for (rhi::TextureHandle& luma : m_luma) {
        DAI_TRY_ASSIGN(luma, CreateTarget(extent, Format::R16Float, "FSR 3.1 luma"));
    }
    for (rhi::TextureHandle& history : m_lumaHistory) {
        DAI_TRY_ASSIGN(history, CreateTarget(extent, Format::Rgba16Float, "FSR 3.1 luma history"));
    }
    DAI_TRY_ASSIGN(m_lumaInstability, CreateTarget(extent, Format::R16Float, "FSR 3.1 luma instability"));
    DAI_TRY_ASSIGN(m_shadingChange, CreateTarget(half, Format::R8Unorm, "FSR 3.1 shading change"));
    for (std::uint32_t level = 0; level < m_spdMips.size(); ++level) {
        DAI_TRY_ASSIGN(m_spdMips[level],
                       CreateTarget(rhi::MipExtent(half, level), Format::Rg16Float, "FSR 3.1 luma pyramid"));
    }
    DAI_TRY_ASSIGN(m_spdAtomic, CreateTarget({1, 1}, Format::R32Uint, "FSR 3.1 pyramid counter"));
    DAI_TRY_ASSIGN(m_frameInfo, CreateTarget({1, 1}, Format::Rgba32Float, "FSR 3.1 frame info"));
    for (rhi::TextureHandle& accumulation : m_accumulation) {
        DAI_TRY_ASSIGN(accumulation, CreateTarget(extent, Format::R8Unorm, "FSR 3.1 accumulation"));
    }
    DAI_TRY_ASSIGN(m_newLocks, CreateTarget(extent, Format::R8Unorm, "FSR 3.1 new locks"));
    DAI_TRY_ASSIGN(m_dilatedReactive, CreateTarget(extent, Format::Rgba8Unorm, "FSR 3.1 dilated reactive masks"));
    for (rhi::TextureHandle& history : m_history) {
        DAI_TRY_ASSIGN(history, CreateTarget(extent, Format::Rgba16Float, "FSR 3.1 history"));
    }
    DAI_TRY_ASSIGN(m_output, CreateTarget(extent, Format::Rgba32Float, "FSR 3.1 output"));
    m_extent = extent;
    m_fresh = true;
    m_reset = true;
    return {};
}

void Fsr3Upscaler::AddTo(RenderGraph& graph, const Fsr3Inputs& inputs) {
    const rhi::Extent render = inputs.renderExtent;
    const std::uint32_t write = m_current;
    const std::uint32_t read = 1 - m_current;
    m_current = read;

    // The engine jitters the camera so that a pixel samples its center moved by the jitter; FSR's jitter
    // moves the image, the other way.
    const glm::vec2 jitter = -inputs.jitter;
    // The phases of the jitter, as AMD counts them, which only FSR's locks use: 8 / scale².
    const float ratio = static_cast<float>(m_extent.width) / static_cast<float>(std::max(render.width, 1U));
    const float phases = std::floor(8.0f * ratio * ratio);
    if (m_reset || m_jitterSequenceLength == 0.0f) {
        m_jitterSequenceLength = phases;
    } else if (phases > m_jitterSequenceLength) {
        m_jitterSequenceLength += 1.0f;
    } else if (phases < m_jitterSequenceLength) {
        m_jitterSequenceLength -= 1.0f;
    }
    m_frameIndex = m_reset ? 0.0f : m_frameIndex + 1.0f;

    // Reverse-Z with a far plane: AMD's setupDeviceDepthToViewSpaceDepthParams, inverted and finite.
    const Perspective& perspective = inputs.perspective;
    const float nearPlane = perspective.nearPlane;
    const float farPlane = std::max(perspective.farPlane, nearPlane * 2.0f);
    const float q = nearPlane / (farPlane - nearPlane);
    const float tanHalfVertical = std::tan(perspective.verticalFov * 0.5f);
    const float cotHalfVertical = 1.0f / std::max(tanHalfVertical, 1e-6f);

    // The luma pyramid covers the render size (AMD's ffxSpdSetup, with as many levels as it has).
    const std::uint32_t tilesX = (render.width + kSpdTile - 1) / kSpdTile;
    const std::uint32_t tilesY = (render.height + kSpdTile - 1) / kSpdTile;
    const auto spdMips = static_cast<std::uint32_t>(
        std::min(std::floor(std::log2(static_cast<float>(std::max({render.width, render.height, 1U})))), 12.0f));

    const auto texture = [this](rhi::TextureHandle handle) { return m_device->BindlessIndex(handle); };
    const auto image = [this](rhi::TextureHandle handle) { return m_device->StorageImageIndex(handle); };
    const rhi::Extent half{m_extent.width / 2, m_extent.height / 2};
    Fsr3Data data{.renderWidth = static_cast<std::int32_t>(render.width),
                  .renderHeight = static_cast<std::int32_t>(render.height),
                  .previousRenderWidth = static_cast<std::int32_t>(m_previousRenderExtent.width),
                  .previousRenderHeight = static_cast<std::int32_t>(m_previousRenderExtent.height),
                  .upscaleWidth = static_cast<std::int32_t>(m_extent.width),
                  .upscaleHeight = static_cast<std::int32_t>(m_extent.height),
                  .previousUpscaleWidth = static_cast<std::int32_t>(m_extent.width),
                  .previousUpscaleHeight = static_cast<std::int32_t>(m_extent.height),
                  .maxRenderWidth = static_cast<std::int32_t>(m_extent.width),
                  .maxRenderHeight = static_cast<std::int32_t>(m_extent.height),
                  .maxUpscaleWidth = static_cast<std::int32_t>(m_extent.width),
                  .maxUpscaleHeight = static_cast<std::int32_t>(m_extent.height),
                  .deviceToViewDepth = {-q, q * farPlane, perspective.aspect / cotHalfVertical, 1.0f / cotHalfVertical},
                  .jitterX = jitter.x,
                  .jitterY = jitter.y,
                  .previousJitterX = m_previousJitter.x,
                  .previousJitterY = m_previousJitter.y,
                  // The engine's motion vectors are in texture coordinates of the frame already.
                  .motionVectorScaleX = 1.0f,
                  .motionVectorScaleY = 1.0f,
                  .downscaleFactorX = static_cast<float>(render.width) / static_cast<float>(m_extent.width),
                  .downscaleFactorY = static_cast<float>(render.height) / static_cast<float>(m_extent.height),
                  .motionVectorJitterCancellationX = 0.0f,
                  .motionVectorJitterCancellationY = 0.0f,
                  .tanHalfFov = tanHalfVertical * perspective.aspect,
                  .jitterSequenceLength = m_jitterSequenceLength,
                  .deltaTime = std::clamp(inputs.seconds, 0.0f, 1.0f),
                  .deltaPreExposure = 1.0f,
                  // The engine's units are meters (ADR-0043).
                  .viewSpaceToMetersFactor = 1.0f,
                  .frameIndex = m_frameIndex,
                  .velocityFactor = 1.0f,
                  .reactivenessScale = 1.0f,
                  .shadingChangeScale = 1.0f,
                  .accumulationAddedPerFrame = kAccumulationAddedPerFrame,
                  .minDisocclusionAccumulation = kMinDisocclusionAccumulation,
                  .spdMips = spdMips,
                  .spdWorkGroups = tilesX * tilesY,
                  .spdWorkGroupOffsetX = 0,
                  .spdWorkGroupOffsetY = 0,
                  .spdRenderWidth = render.width,
                  .spdRenderHeight = render.height,
                  .colorTexture = texture(inputs.color),
                  .depthTexture = texture(inputs.depth),
                  .motionTexture = texture(inputs.motion),
                  .reactiveTexture = texture(inputs.reactive),
                  .dilatedMotionTexture = texture(m_dilatedMotion),
                  .dilatedMotionImage = image(m_dilatedMotion),
                  .dilatedDepthTexture = texture(m_dilatedDepth),
                  .dilatedDepthImage = image(m_dilatedDepth),
                  .reconstructedDepthImage = image(m_reconstructedDepth),
                  .farthestDepthTexture = texture(m_farthestDepth),
                  .farthestDepthImage = image(m_farthestDepth),
                  .farthestDepthMip1Texture = texture(m_farthestDepthMip1),
                  .farthestDepthMip1Image = image(m_farthestDepthMip1),
                  .currentLumaTexture = texture(m_luma[write]),
                  .currentLumaImage = image(m_luma[write]),
                  .previousLumaTexture = texture(m_luma[read]),
                  .lumaHistoryTexture = texture(m_lumaHistory[read]),
                  .lumaHistoryImage = image(m_lumaHistory[write]),
                  .lumaInstabilityTexture = texture(m_lumaInstability),
                  .lumaInstabilityImage = image(m_lumaInstability),
                  .shadingChangeTexture = texture(m_shadingChange),
                  .shadingChangeImage = image(m_shadingChange),
                  .spdMipTextures = {},
                  .spdMipImages = {},
                  .spdMipWidth = std::max(half.width, 1U),
                  .spdMipHeight = std::max(half.height, 1U),
                  .spdAtomicImage = image(m_spdAtomic),
                  .frameInfoImage = image(m_frameInfo),
                  .accumulationTexture = texture(m_accumulation[read]),
                  .accumulationImage = image(m_accumulation[write]),
                  .newLocksTexture = texture(m_newLocks),
                  .newLocksImage = image(m_newLocks),
                  .dilatedReactiveTexture = texture(m_dilatedReactive),
                  .dilatedReactiveImage = image(m_dilatedReactive),
                  .historyTexture = texture(m_history[read]),
                  .historyImage = image(m_history[write]),
                  .outputImage = image(m_output)};
    for (std::size_t level = 0; level < m_spdMips.size(); ++level) {
        data.spdMipTextures[level] = texture(m_spdMips[level]);
        data.spdMipImages[level] = image(m_spdMips[level]);
    }
    const rhi::BufferHandle buffer = m_data[m_dataSlot];
    m_dataSlot = (m_dataSlot + 1) % rhi::kFramesInFlight;
    std::memcpy(m_device->Mapped(buffer).data(), &data, sizeof(data));
    const Fsr3PushConstants push{m_device->StorageIndex(buffer)};

    constexpr std::string_view kGroup = "FSR 3.1";
    const auto clear = [&graph](rhi::TextureHandle target, glm::vec4 value) {
        graph.AddPass({.name = "FSR 3.1 clear",
                       .group = kGroup,
                       .rendering = rhi::RenderingDesc{.colorTarget = target, .clearColor = value}},
                      [](rhi::CommandList& /*commands*/) {});
    };
    // What AMD's context clears: everything once, the history's weight and the pyramid's average on a reset,
    // and each frame the reprojected depth, which is written with atomics, and the pyramid.
    if (m_fresh) {
        for (const rhi::TextureHandle target : m_targets) {
            if (target != m_output) {
                clear(target, glm::vec4(0.0f));
            }
        }
        m_fresh = false;
    } else if (m_reset) {
        clear(m_accumulation[read], glm::vec4(0.0f));
        clear(m_newLocks, glm::vec4(0.0f));
    }
    if (m_reset) {
        clear(m_frameInfo, glm::vec4(-1.0f, 1.0f, 0.0f, 0.0f));
    }
    clear(m_reconstructedDepth, glm::vec4(0.0f));
    clear(m_spdAtomic, glm::vec4(0.0f));
    for (const rhi::TextureHandle level : m_spdMips) {
        clear(level, glm::vec4(0.0f));
    }

    using rhi::Access;
    const auto dispatch = [&](Pass pass, std::string_view name, std::vector<rhi::TextureUse> textures,
                              std::uint32_t groupsX, std::uint32_t groupsY) {
        graph.AddPass({.name = name, .group = kGroup, .textures = std::move(textures)},
                      [this, pass, push, groupsX, groupsY](rhi::CommandList& commands) {
                          DAI_PROFILE_SCOPE("renderer::Fsr3");
                          commands.BindPipeline(m_pipelines[pass]);
                          commands.Push(push);
                          commands.Dispatch(groupsX, groupsY);
                      });
    };
    std::vector<rhi::TextureUse> pyramid;
    for (const rhi::TextureHandle level : m_spdMips) {
        pyramid.push_back({level, Access::ComputeShaderWrite});
    }
    const auto with = [](std::vector<rhi::TextureUse> uses, const std::vector<rhi::TextureUse>& more) {
        uses.insert(uses.end(), more.begin(), more.end());
        return uses;
    };

    dispatch(PrepareInputs, "FSR 3.1 prepare inputs",
             {{inputs.color, Access::ComputeShaderRead},
              {inputs.depth, Access::ComputeShaderRead},
              {inputs.motion, Access::ComputeShaderRead},
              {m_dilatedMotion, Access::ComputeShaderWrite},
              {m_dilatedDepth, Access::ComputeShaderWrite},
              {m_reconstructedDepth, Access::ComputeShaderWrite},
              {m_farthestDepth, Access::ComputeShaderWrite},
              {m_luma[write], Access::ComputeShaderWrite}},
             Groups(render.width), Groups(render.height));
    dispatch(LumaPyramid, "FSR 3.1 luma pyramid",
             with({{m_luma[write], Access::ComputeShaderRead},
                   {m_farthestDepth, Access::ComputeShaderRead},
                   {m_spdAtomic, Access::ComputeShaderWrite},
                   {m_frameInfo, Access::ComputeShaderWrite},
                   {m_farthestDepthMip1, Access::ComputeShaderWrite}},
                  pyramid),
             tilesX, tilesY);
    // The counter goes back to 0 at the end of each pyramid, for the next.
    dispatch(ShadingChangePyramid, "FSR 3.1 shading change pyramid",
             with({{m_luma[write], Access::ComputeShaderRead},
                   {m_luma[read], Access::ComputeShaderRead},
                   {m_dilatedMotion, Access::ComputeShaderRead},
                   {m_spdAtomic, Access::ComputeShaderWrite},
                   {m_frameInfo, Access::ComputeShaderWrite}},
                  pyramid),
             tilesX, tilesY);
    std::vector<rhi::TextureUse> sampledPyramid;
    for (const rhi::TextureHandle level : m_spdMips) {
        sampledPyramid.push_back({level, Access::ComputeShaderRead});
    }
    dispatch(ShadingChange, "FSR 3.1 shading change",
             with({{m_shadingChange, Access::ComputeShaderWrite}, {m_frameInfo, Access::ComputeShaderWrite}},
                  sampledPyramid),
             Groups(render.width / 2), Groups(render.height / 2));
    dispatch(PrepareReactivity, "FSR 3.1 prepare reactivity",
             {{m_reconstructedDepth, Access::ComputeShaderWrite},
              {m_dilatedMotion, Access::ComputeShaderRead},
              {m_dilatedDepth, Access::ComputeShaderRead},
              {inputs.reactive, Access::ComputeShaderRead},
              {m_accumulation[read], Access::ComputeShaderRead},
              {m_shadingChange, Access::ComputeShaderRead},
              {m_luma[write], Access::ComputeShaderRead},
              {m_frameInfo, Access::ComputeShaderWrite},
              {m_dilatedReactive, Access::ComputeShaderWrite},
              {m_newLocks, Access::ComputeShaderWrite},
              {m_accumulation[write], Access::ComputeShaderWrite}},
             Groups(render.width), Groups(render.height));
    dispatch(LumaInstability, "FSR 3.1 luma instability",
             {{m_dilatedReactive, Access::ComputeShaderRead},
              {m_dilatedMotion, Access::ComputeShaderRead},
              {m_frameInfo, Access::ComputeShaderWrite},
              {m_lumaHistory[read], Access::ComputeShaderRead},
              {m_farthestDepthMip1, Access::ComputeShaderRead},
              {m_luma[write], Access::ComputeShaderRead},
              {m_lumaHistory[write], Access::ComputeShaderWrite},
              {m_lumaInstability, Access::ComputeShaderWrite}},
             Groups(render.width), Groups(render.height));
    dispatch(Accumulate, "FSR 3.1 accumulate",
             {{m_dilatedReactive, Access::ComputeShaderRead},
              {m_dilatedMotion, Access::ComputeShaderRead},
              {m_history[read], Access::ComputeShaderRead},
              {m_farthestDepthMip1, Access::ComputeShaderRead},
              {m_luma[write], Access::ComputeShaderRead},
              {m_lumaInstability, Access::ComputeShaderRead},
              {inputs.color, Access::ComputeShaderRead},
              {m_frameInfo, Access::ComputeShaderWrite},
              {m_history[write], Access::ComputeShaderWrite},
              {m_output, Access::ComputeShaderWrite},
              {m_newLocks, Access::ComputeShaderWrite}},
             Groups(m_extent.width), Groups(m_extent.height));

    m_previousRenderExtent = render;
    m_previousJitter = jitter;
    m_reset = false;
}

} // namespace daidalos::renderer
