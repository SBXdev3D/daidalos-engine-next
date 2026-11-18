// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstdint>
#include <optional>

namespace daidalos::renderer {

class RenderGraph;

/// Reads a frame back for a photo (ADR-0077): the image as displayed but without the interface, which the
/// renderer draws into DisplayTarget with the output pass, and the scene's light it was made from. The copies
/// are recorded into the frame, and read when the frame's slot comes back, once the GPU has finished it.
class FrameCapture {
public:
    explicit FrameCapture(rhi::Device& device) : m_device(&device) {}
    FrameCapture(FrameCapture&& other) noexcept;
    FrameCapture& operator=(FrameCapture&&) = delete;
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    ~FrameCapture();

    void Request() { m_requested = true; }
    /// A capture was asked for and is not recorded yet: this frame records it.
    [[nodiscard]] bool Requested() const { return m_requested && !m_slot.has_value(); }

    /// The RGBA8 sRGB target the output pass draws the displayed image into, at the window's size.
    [[nodiscard]] core::Result<rhi::TextureHandle> DisplayTarget(rhi::Extent extent);
    /// Records the copies: the display target, and the scene's light, an Rgba16Float or Rgba32Float texture
    /// whose top left sceneExtent is the image, multiplied by exposure as the output pass does. A scene of
    /// another format is left out.
    [[nodiscard]] core::Result<void> AddCopies(RenderGraph& graph, std::uint32_t slot,
                                               std::optional<rhi::TextureHandle> scene, rhi::Extent sceneExtent,
                                               float exposure);
    /// At the start of a frame of this slot: reads the capture its last frame recorded.
    void Read(std::uint32_t slot);
    [[nodiscard]] std::optional<CapturedFrame> Take();

private:
    void Release();

    rhi::Device* m_device;
    bool m_requested = false;
    std::optional<std::uint32_t> m_slot;
    rhi::TextureHandle m_display;
    rhi::Extent m_displayExtent{};
    rhi::BufferHandle m_displayBuffer;
    rhi::BufferHandle m_sceneBuffer;
    rhi::Extent m_sceneTexture{};
    rhi::Extent m_sceneExtent{};
    rhi::Format m_sceneFormat = rhi::Format::Undefined;
    float m_exposure = 1.0f;
    std::optional<CapturedFrame> m_done;
};

} // namespace daidalos::renderer
