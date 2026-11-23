// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

namespace daidalos::renderer {

class RenderGraph;

/// Reads a frame back for a photo (ADR-0077): the image as displayed, without the interface for a photo, which
/// the renderer draws into DisplayTarget with the output pass, and the scene's light it was made from. The copies
/// are recorded into the frame, and read when the frame's slot comes back, once the GPU has finished it. Each frame
/// slot holds its own, so that a recording (ADR-0123) reads every frame back.
class FrameCapture {
public:
    explicit FrameCapture(rhi::Device& device) : m_device(&device) {}
    FrameCapture(FrameCapture&& other) noexcept;
    FrameCapture& operator=(FrameCapture&&) = delete;
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    ~FrameCapture();

    /// Captures the next frame drawn: a photo, or with video a frame of a recording (ADR-0123), with the
    /// interface over it, as the fades are, and without the scene's light.
    void Request(bool video) { m_request = video; }
    /// A capture was asked for and is not recorded yet: this frame records it.
    [[nodiscard]] bool Requested() const { return m_request.has_value(); }
    [[nodiscard]] bool Video() const { return m_request.value_or(false); }

    /// The RGBA8 sRGB target the output pass draws the displayed image into for this slot's frame, at the
    /// window's size.
    [[nodiscard]] core::Result<rhi::TextureHandle> DisplayTarget(std::uint32_t slot, rhi::Extent extent);
    /// Records the copies: the display target, and the scene's light, an Rgba16Float or Rgba32Float texture
    /// whose top left sceneExtent is the image, multiplied by exposure as the output pass does. A scene of
    /// another format is left out. The request is then met.
    [[nodiscard]] core::Result<void> AddCopies(RenderGraph& graph, std::uint32_t slot,
                                               std::optional<rhi::TextureHandle> scene, rhi::Extent sceneExtent,
                                               float exposure);
    /// At the start of a frame of this slot: reads the capture its last frame recorded.
    void Read(std::uint32_t slot);
    /// The frames read, oldest first.
    [[nodiscard]] std::optional<CapturedFrame> Take();
    /// Captures recorded and not read yet.
    [[nodiscard]] std::size_t InFlight() const;

private:
    struct Slot {
        bool recorded = false;
        rhi::TextureHandle display;
        rhi::Extent displayExtent{};
        rhi::BufferHandle displayBuffer;
        rhi::BufferHandle sceneBuffer;
        std::uint64_t sceneBytes = 0;
        rhi::Extent sceneTexture{};
        rhi::Extent sceneExtent{};
        rhi::Format sceneFormat = rhi::Format::Undefined;
        float exposure = 1.0f;
    };
    void Release(Slot& slot);

    rhi::Device* m_device;
    std::optional<bool> m_request;
    std::array<Slot, rhi::kFramesInFlight> m_slots{};
    std::deque<CapturedFrame> m_done;
};

} // namespace daidalos::renderer
