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
    : m_device(other.m_device), m_request(std::exchange(other.m_request, std::nullopt)),
      m_slots(std::exchange(other.m_slots, {})), m_done(std::move(other.m_done)) {}

FrameCapture::~FrameCapture() {
    for (Slot& slot : m_slots) {
        Release(slot);
    }
}

void FrameCapture::Release(Slot& slot) {
    if (const rhi::TextureHandle display = std::exchange(slot.display, {}); !display.IsNull()) {
        m_device->DestroyTexture(display);
    }
    for (rhi::BufferHandle* buffer : {&slot.displayBuffer, &slot.sceneBuffer}) {
        if (const rhi::BufferHandle released = std::exchange(*buffer, {}); !released.IsNull()) {
            m_device->DestroyBuffer(released);
        }
    }
    slot.displayExtent = {};
    slot.sceneBytes = 0;
    slot.recorded = false;
}

core::Result<rhi::TextureHandle> FrameCapture::DisplayTarget(std::uint32_t index, rhi::Extent extent) {
    DAI_ASSERT(index < m_slots.size(), "A frame slot");
    Slot& slot = m_slots[index];
    if (!slot.display.IsNull() && slot.displayExtent.width == extent.width &&
        slot.displayExtent.height == extent.height) {
        return slot.display;
    }
    Release(slot);
    DAI_TRY_ASSIGN(slot.display,
                   m_device->CreateTexture({.extent = extent,
                                            .format = rhi::Format::Rgba8Srgb,
                                            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                            .name = "Capture"}));
    DAI_TRY_ASSIGN(slot.displayBuffer, m_device->CreateBuffer({.size = std::uint64_t{extent.width} * extent.height * 4,
                                                               .usage = rhi::BufferUsage::TransferDst,
                                                               .memory = rhi::MemoryLocation::GpuToCpu,
                                                               .name = "Capture readback"}));
    slot.displayExtent = extent;
    return slot.display;
}

core::Result<void> FrameCapture::AddCopies(RenderGraph& graph, std::uint32_t index,
                                           std::optional<rhi::TextureHandle> scene, rhi::Extent sceneExtent,
                                           float exposure) {
    DAI_ASSERT(index < m_slots.size(), "A frame slot");
    Slot& slot = m_slots[index];
    DAI_ASSERT(!slot.display.IsNull(), "The display target is drawn before its copy");
    slot.sceneFormat = rhi::Format::Undefined;
    if (scene.has_value()) {
        const rhi::Format format = m_device->TextureFormat(*scene);
        const std::uint64_t pixelBytes = format == rhi::Format::Rgba32Float   ? 16
                                         : format == rhi::Format::Rgba16Float ? 8
                                                                              : 0;
        if (pixelBytes > 0) {
            slot.sceneTexture = m_device->TextureExtent(*scene);
            const std::uint64_t bytes = std::uint64_t{slot.sceneTexture.width} * slot.sceneTexture.height * pixelBytes;
            if (slot.sceneBuffer.IsNull() || slot.sceneBytes != bytes) {
                if (const rhi::BufferHandle released = std::exchange(slot.sceneBuffer, {}); !released.IsNull()) {
                    m_device->DestroyBuffer(released);
                }
                DAI_TRY_ASSIGN(slot.sceneBuffer, m_device->CreateBuffer({.size = bytes,
                                                                         .usage = rhi::BufferUsage::TransferDst,
                                                                         .memory = rhi::MemoryLocation::GpuToCpu,
                                                                         .name = "Capture scene readback"}));
                slot.sceneBytes = bytes;
            }
            slot.sceneFormat = format;
            slot.sceneExtent = {std::min(sceneExtent.width, slot.sceneTexture.width),
                                std::min(sceneExtent.height, slot.sceneTexture.height)};
        }
    }
    slot.exposure = exposure;

    std::vector<rhi::TextureUse> textures{{slot.display, rhi::Access::TransferRead}};
    std::vector<rhi::BufferUse> buffers{{slot.displayBuffer, rhi::Access::TransferWrite}};
    const bool withScene = slot.sceneFormat != rhi::Format::Undefined;
    if (withScene) {
        textures.push_back({*scene, rhi::Access::TransferRead});
        buffers.push_back({slot.sceneBuffer, rhi::Access::TransferWrite});
    }
    const rhi::TextureHandle display = slot.display;
    const rhi::BufferHandle displayBuffer = slot.displayBuffer;
    const rhi::BufferHandle sceneBuffer = withScene ? slot.sceneBuffer : rhi::BufferHandle{};
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
    slot.recorded = true;
    m_request.reset();
    return {};
}

void FrameCapture::Read(std::uint32_t index) {
    if (index >= m_slots.size()) {
        return;
    }
    Slot& slot = m_slots[index];
    if (!slot.recorded) {
        return;
    }
    DAI_PROFILE_SCOPE("renderer::FrameCapture::Read");
    CapturedFrame frame;
    frame.width = slot.displayExtent.width;
    frame.height = slot.displayExtent.height;
    const std::span<const std::byte> display = m_device->Mapped(slot.displayBuffer);
    frame.display.resize(std::size_t{frame.width} * frame.height * 4);
    std::memcpy(frame.display.data(), display.data(), frame.display.size());
    if (slot.sceneFormat != rhi::Format::Undefined) {
        const std::span<const std::byte> scene = m_device->Mapped(slot.sceneBuffer);
        frame.sceneWidth = slot.sceneExtent.width;
        frame.sceneHeight = slot.sceneExtent.height;
        frame.scene.resize(std::size_t{frame.sceneWidth} * frame.sceneHeight * 3);
        const bool wide = slot.sceneFormat == rhi::Format::Rgba32Float;
        for (std::uint32_t y = 0; y < frame.sceneHeight; ++y) {
            for (std::uint32_t x = 0; x < frame.sceneWidth; ++x) {
                const std::size_t texel = (std::size_t{y} * slot.sceneTexture.width) + x;
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
                    out[channel] = std::isfinite(value) ? std::max(value * slot.exposure, 0.0f) : 0.0f;
                }
            }
        }
    }
    m_done.push_back(std::move(frame));
    slot.recorded = false;
    // A single photo gives its memory back; a recording keeps it for the next frame of this slot.
    if (!m_request.has_value()) {
        Release(slot);
    }
}

std::optional<CapturedFrame> FrameCapture::Take() {
    if (m_done.empty()) {
        return std::nullopt;
    }
    CapturedFrame frame = std::move(m_done.front());
    m_done.pop_front();
    return frame;
}

std::size_t FrameCapture::InFlight() const {
    return static_cast<std::size_t>(std::ranges::count(m_slots, true, &Slot::recorded));
}

} // namespace daidalos::renderer
