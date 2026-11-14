// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// Positions the camera's jitter goes through at the output's resolution, one per frame, before it starts
/// again.
inline constexpr std::uint32_t kJitterPositions = 8;

/// Positions the jitter goes through at a render scale (render size over output size, per axis): 8 / scale²,
/// so that each output pixel gets as many samples near its center as at the output's resolution
/// (ADR-0049).
[[nodiscard]] std::uint32_t JitterPositions(float scale);

/// Where frame number `frame` samples each pixel, relative to its center, in render pixels from -0.5 to 0.5,
/// x to the right and y down: the Halton sequence in bases 2 and 3, which covers the pixel evenly
/// whatever the number of frames, over `positions` points.
[[nodiscard]] glm::vec2 Jitter(std::uint64_t frame, std::uint32_t positions = kJitterPositions);

/// What the temporal resolve reads each frame.
struct TemporalResolveInputs {
    /// Storage index of the frame's FrameData.
    std::uint32_t frameBuffer = 0;
    /// HDR color, rendered with the jitter in the top-left corner (FrameData::renderSize) of a target
    /// that may be larger than the output.
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    /// The G-buffer's motion vectors.
    rhi::TextureHandle motion;
    /// How little to trust the history, from 0 to 1 (ParticlePass::kReactiveFormat).
    rhi::TextureHandle reactive;
};

/// Temporal antialiasing and upscaling (ADR-0037, ADR-0049): blends each frame, drawn at the render
/// resolution, with the history of the earlier ones, at the output's. Its inputs are those FSR takes, so that
/// another upscaler could stand in for it. The Device must outlive it.
class TemporalResolvePass {
public:
    static core::Result<TemporalResolvePass> Create(rhi::Device& device);

    TemporalResolvePass(TemporalResolvePass&& other) noexcept;
    TemporalResolvePass& operator=(TemporalResolvePass&& other) = delete;
    TemporalResolvePass(const TemporalResolvePass&) = delete;
    TemporalResolvePass& operator=(const TemporalResolvePass&) = delete;
    ~TemporalResolvePass();

    /// Follows the size of the output. A new size drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Drops the history: the next frame is resolved alone.
    void Reset() { m_hasHistory = false; }
    /// Adds the pass that resolves the frame into a new history, as "Temporal resolve". Valid after
    /// Resize.
    void AddTo(RenderGraph& graph, const TemporalResolveInputs& inputs);

    /// The resolved frame at the output's size, in linear HDR, which is also the next frame's history; alpha
    /// holds the weight of the samples it has.
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
