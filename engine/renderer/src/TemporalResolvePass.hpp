// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// Positions the camera's jitter goes through, one per frame, before it starts again.
inline constexpr std::uint32_t kJitterPositions = 8;

/// Where frame number `frame` samples each pixel, relative to its center, in pixels from -0.5 to 0.5,
/// x to the right and y down: the Halton sequence in bases 2 and 3, which covers the pixel evenly
/// whatever the number of frames.
[[nodiscard]] glm::vec2 Jitter(std::uint64_t frame);

/// What the temporal resolve reads each frame.
struct TemporalResolveInputs {
    /// Storage index of the frame's FrameData.
    std::uint32_t frameBuffer = 0;
    /// HDR color, rendered with the jitter.
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    /// The pre-pass's motion vectors.
    rhi::TextureHandle motion;
    /// How little to trust the history, from 0 to 1 (ParticlePass::kReactiveFormat).
    rhi::TextureHandle reactive;
};

/// Temporal antialiasing (ADR-0037): blends each frame with the history of the earlier ones. The Device
/// must outlive it.
class TemporalResolvePass {
public:
    static core::Result<TemporalResolvePass> Create(rhi::Device& device);

    TemporalResolvePass(TemporalResolvePass&& other) noexcept;
    TemporalResolvePass& operator=(TemporalResolvePass&& other) = delete;
    TemporalResolvePass(const TemporalResolvePass&) = delete;
    TemporalResolvePass& operator=(const TemporalResolvePass&) = delete;
    ~TemporalResolvePass();

    /// Follows the size of the frame. A new size drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Drops the history: the next frame is resolved alone.
    void Reset() { m_hasHistory = false; }
    /// Adds the pass that resolves the frame into a new history, as "Temporal resolve". Valid after
    /// Resize.
    void AddTo(RenderGraph& graph, const TemporalResolveInputs& inputs);

    /// The resolved frame, in linear HDR, which is also the next frame's history.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_history[m_current]; }

private:
    explicit TemporalResolvePass(rhi::Device& device);

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    /// One holds the last frame's result while the other receives this frame's.
    std::array<rhi::TextureHandle, 2> m_history;
    std::uint32_t m_current = 0;
    rhi::Extent m_extent;
    bool m_hasHistory = false;
};

} // namespace daidalos::renderer
