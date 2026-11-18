// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "FrameCapture.hpp"

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

/// An IEEE half to a float.
float HalfToFloat(std::uint16_t half) {
    const std::uint32_t sign = (half >> 15U) & 1U;
    const std::uint32_t exponent = (half >> 10U) & 0x1FU;
    const std::uint32_t mantissa = half & 0x3FFU;
    float value = 0.0f;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa == 0 ? INFINITY : NAN;
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<int>(exponent) - 25);
    }
    return sign != 0 ? -value : value;
}

} // namespace

FrameCapture::FrameCapture(FrameCapture&& other) noexcept
    : m_device(other.m_device), m_requested(other.m_requested), m_slot(other.m_slot),
      m_display(std::exchange(other.m_display, {})), m_displayExtent(other.m_displayExtent),
      m_displayBuffer(std::exchange(other.m_displayBuffer, {})), m_sceneBuffer(std::exchange(other.m_sceneBuffer, {})),
      m_sceneTexture(other.m_sceneTexture), m_sceneExtent(other.m_sceneExtent), m_sceneFormat(other.m_sceneFormat),
      m_exposure(other.m_exposure), m_done(std::move(other.m_done)) {}

FrameCapture::~FrameCapture() {
    Release();
}

void FrameCapture::Release() {
    if (const rhi::TextureHandle display = std::exchange(m_display, {}); !display.IsNull()) {
        m_device->DestroyTexture(display);
    }
    for (rhi::BufferHandle* buffer : {&m_displayBuffer, &m_sceneBuffer}) {
        if (const rhi::BufferHandle released = std::exchange(*buffer, {}); !released.IsNull()) {
            m_device->DestroyBuffer(released);
        }
    }
    m_displayExtent = {};
}

core::Result<rhi::TextureHandle> FrameCapture::DisplayTarget(rhi::Extent extent) {
    if (!m_display.IsNull() && m_displayExtent.width == extent.width && m_displayExtent.height == extent.height) {
        return m_display;
    }
    if (!m_display.IsNull()) {
        m_device->DestroyTexture(std::exchange(m_display, {}));
    }
    DAI_TRY_ASSIGN(m_display,
                   m_device->CreateTexture({.extent = extent,
                                            .format = rhi::Format::Rgba8Srgb,
                                            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                            .name = "Capture"}));
    m_displayExtent = extent;
    return m_display;
}

core::Result<void> FrameCapture::AddCopies(RenderGraph& graph, std::uint32_t slot,
                                           std::optional<rhi::TextureHandle> scene, rhi::Extent sceneExtent,
                                           float exposure) {
    DAI_ASSERT(!m_display.IsNull(), "The display target is drawn before its copy");
    for (rhi::BufferHandle* buffer : {&m_displayBuffer, &m_sceneBuffer}) {
        if (const rhi::BufferHandle released = std::exchange(*buffer, {}); !released.IsNull()) {
            m_device->DestroyBuffer(released);
        }
    }
    DAI_TRY_ASSIGN(m_displayBuffer,
                   m_device->CreateBuffer({.size = std::uint64_t{m_displayExtent.width} * m_displayExtent.height * 4,
                                           .usage = rhi::BufferUsage::TransferDst,
                                           .memory = rhi::MemoryLocation::GpuToCpu,
                                           .name = "Capture readback"}));
    m_sceneFormat = rhi::Format::Undefined;
    if (scene.has_value()) {
        const rhi::Format format = m_device->TextureFormat(*scene);
        const std::uint64_t pixelBytes = format == rhi::Format::Rgba32Float   ? 16
                                         : format == rhi::Format::Rgba16Float ? 8
                                                                              : 0;
        if (pixelBytes > 0) {
            m_sceneTexture = m_device->TextureExtent(*scene);
            DAI_TRY_ASSIGN(m_sceneBuffer, m_device->CreateBuffer({.size = std::uint64_t{m_sceneTexture.width} *
                                                                          m_sceneTexture.height * pixelBytes,
                                                                  .usage = rhi::BufferUsage::TransferDst,
                                                                  .memory = rhi::MemoryLocation::GpuToCpu,
                                                                  .name = "Capture scene readback"}));
            m_sceneFormat = format;
            m_sceneExtent = {std::min(sceneExtent.width, m_sceneTexture.width),
                             std::min(sceneExtent.height, m_sceneTexture.height)};
        }
    }
    m_exposure = exposure;

    std::vector<rhi::TextureUse> textures{{m_display, rhi::Access::TransferRead}};
    std::vector<rhi::BufferUse> buffers{{m_displayBuffer, rhi::Access::TransferWrite}};
    const bool withScene = m_sceneFormat != rhi::Format::Undefined;
    if (withScene) {
        textures.push_back({*scene, rhi::Access::TransferRead});
        buffers.push_back({m_sceneBuffer, rhi::Access::TransferWrite});
    }
    const rhi::TextureHandle display = m_display;
    const rhi::BufferHandle displayBuffer = m_displayBuffer;
    const rhi::BufferHandle sceneBuffer = m_sceneBuffer;
    const rhi::TextureHandle sceneTexture = withScene ? *scene : rhi::TextureHandle{};
    graph.AddPass({.name = "Capture readback",
                   .group = "Capture",
                   .textures = std::move(textures),
                   .buffers = std::move(buffers)},
                  [=](rhi::CommandList& commands) {
                      commands.CopyTextureToBuffer(display, displayBuffer);
                      if (!sceneBuffer.IsNull()) {
                          commands.CopyTextureToBuffer(sceneTexture, sceneBuffer);
                      }
                  });
    m_slot = slot;
    return {};
}

void FrameCapture::Read(std::uint32_t slot) {
    if (!m_slot.has_value() || *m_slot != slot) {
        return;
    }
    DAI_PROFILE_SCOPE("renderer::FrameCapture::Read");
    CapturedFrame frame;
    frame.width = m_displayExtent.width;
    frame.height = m_displayExtent.height;
    const std::span<const std::byte> display = m_device->Mapped(m_displayBuffer);
    frame.display.resize(std::size_t{frame.width} * frame.height * 4);
    std::memcpy(frame.display.data(), display.data(), frame.display.size());
    if (m_sceneFormat != rhi::Format::Undefined) {
        const std::span<const std::byte> scene = m_device->Mapped(m_sceneBuffer);
        frame.sceneWidth = m_sceneExtent.width;
        frame.sceneHeight = m_sceneExtent.height;
        frame.scene.resize(std::size_t{frame.sceneWidth} * frame.sceneHeight * 3);
        const bool wide = m_sceneFormat == rhi::Format::Rgba32Float;
        for (std::uint32_t y = 0; y < frame.sceneHeight; ++y) {
            for (std::uint32_t x = 0; x < frame.sceneWidth; ++x) {
                const std::size_t texel = (std::size_t{y} * m_sceneTexture.width) + x;
                float* out = &frame.scene[((std::size_t{y} * frame.sceneWidth) + x) * 3];
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    float value = 0.0f;
                    if (wide) {
                        std::memcpy(&value, scene.data() + (((texel * 4) + channel) * 4), 4);
                    } else {
                        std::uint16_t half = 0;
                        std::memcpy(&half, scene.data() + (((texel * 4) + channel) * 2), 2);
                        value = HalfToFloat(half);
                    }
                    out[channel] = std::isfinite(value) ? std::max(value * m_exposure, 0.0f) : 0.0f;
                }
            }
        }
    }
    m_done = std::move(frame);
    m_slot.reset();
    m_requested = false;
    Release();
}

std::optional<CapturedFrame> FrameCapture::Take() {
    return std::exchange(m_done, std::nullopt);
}

} // namespace daidalos::renderer
