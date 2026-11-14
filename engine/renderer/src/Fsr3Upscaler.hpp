// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "MeshPass.hpp"

namespace daidalos::renderer {

class RenderGraph;

/// What FSR reads each frame: the temporal resolve's inputs, and what it needs to know of the camera.
struct Fsr3Inputs {
    /// HDR color, jittered, in the top-left corner (renderExtent) of a target of the output's size.
    rhi::TextureHandle color;
    /// Reverse-Z.
    rhi::TextureHandle depth;
    /// The G-buffer's motion vectors, unjittered, in texture coordinates of the frame.
    rhi::TextureHandle motion;
    /// How little to trust the history, from 0 to 1.
    rhi::TextureHandle reactive;
    rhi::Extent renderExtent;
    /// The engine's jitter (Jitter), in render pixels.
    glm::vec2 jitter{0.0f};
    Perspective perspective;
    /// Since the previous frame.
    float seconds = 0.0f;
};

/// AMD FSR 3.1's upscaler (ADR-0050), run by the engine: AMD's compute passes, unchanged, with the engine's
/// resources and render graph in place of AMD's SDK. It takes the temporal resolve's place, from the same
/// inputs; the output pass sharpens what it returns. Its targets are at the output's size, and the frame
/// is drawn at any render size up to it, as the engine's upscaler does (ADR-0049). The Device must outlive
/// it.
class Fsr3Upscaler {
public:
    static core::Result<Fsr3Upscaler> Create(rhi::Device& device);

    Fsr3Upscaler(Fsr3Upscaler&& other) noexcept;
    Fsr3Upscaler& operator=(Fsr3Upscaler&& other) = delete;
    Fsr3Upscaler(const Fsr3Upscaler&) = delete;
    Fsr3Upscaler& operator=(const Fsr3Upscaler&) = delete;
    ~Fsr3Upscaler();

    /// Follows the size of the output. A new size drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Drops the history: the next frame is upscaled alone.
    void Reset() { m_reset = true; }
    /// Adds FSR's passes, in the group "FSR 3.1". Valid after Resize.
    void AddTo(RenderGraph& graph, const Fsr3Inputs& inputs);

    /// The upscaled frame at the output's size, in linear HDR.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_output; }

private:
    explicit Fsr3Upscaler(rhi::Device& device);
    void DestroyTargets();
    [[nodiscard]] core::Result<rhi::TextureHandle> CreateTarget(rhi::Extent extent, rhi::Format format,
                                                                const char* name);

    rhi::Device* m_device;
    std::array<rhi::PipelineHandle, 7> m_pipelines{};
    /// One per frame in flight: FSR's constants and where its resources are.
    std::array<rhi::BufferHandle, rhi::kFramesInFlight> m_data{};
    std::uint32_t m_dataSlot = 0;

    // FSR's resources, named as AMD's (ffx_fsr3upscaler_resources.h). Pairs are read one frame and
    // written the next.
    rhi::TextureHandle m_dilatedMotion;
    rhi::TextureHandle m_dilatedDepth;
    rhi::TextureHandle m_reconstructedDepth;
    rhi::TextureHandle m_farthestDepth;
    rhi::TextureHandle m_farthestDepthMip1;
    std::array<rhi::TextureHandle, 2> m_luma{};
    std::array<rhi::TextureHandle, 2> m_lumaHistory{};
    rhi::TextureHandle m_lumaInstability;
    rhi::TextureHandle m_shadingChange;
    std::array<rhi::TextureHandle, 6> m_spdMips{};
    rhi::TextureHandle m_spdAtomic;
    rhi::TextureHandle m_frameInfo;
    std::array<rhi::TextureHandle, 2> m_accumulation{};
    rhi::TextureHandle m_newLocks;
    rhi::TextureHandle m_dilatedReactive;
    std::array<rhi::TextureHandle, 2> m_history{};
    rhi::TextureHandle m_output;
    std::vector<rhi::TextureHandle> m_targets;

    rhi::Extent m_extent;
    /// Which of each pair this frame writes.
    std::uint32_t m_current = 0;
    bool m_reset = true;
    /// No frame has been upscaled since the targets were made: they are cleared first.
    bool m_fresh = true;

    // Carried from one frame to the next, as AMD's context does.
    rhi::Extent m_previousRenderExtent;
    glm::vec2 m_previousJitter{0.0f};
    float m_jitterSequenceLength = 0.0f;
    float m_frameIndex = 0.0f;
};

} // namespace daidalos::renderer
