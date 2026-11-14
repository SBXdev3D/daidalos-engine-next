// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// What the denoiser reads each frame.
struct DenoiserInputs {
    /// The pixels the frame draws, in the top-left corner of the targets (ADR-0049); Resize's size when zero.
    rhi::Extent extent;
    /// Storage index of the frame's FrameData.
    std::uint32_t frameBuffer = 0;
    /// The lighting pass's diffuse light per unit of diffuse color, with 1 in alpha where it may be noisy
    /// (MeshPass::kLightFormat).
    rhi::TextureHandle diffuse;
    /// The lighting pass's specular light per unit of specular albedo (MeshPass::kLightFormat).
    rhi::TextureHandle specular;
    rhi::TextureHandle depth;
    /// The G-buffer's normals and roughness.
    rhi::TextureHandle normals;
    /// The G-buffer's motion vectors.
    rhi::TextureHandle motion;
    /// The reflections, whose hit distance in alpha moves the specular history of smooth surfaces with the
    /// reflected point (GlobalIllumination::Reflections, ADR-0042). Null: it moves with the surface.
    rhi::TextureHandle reflections;
};

/// The engine's own spatiotemporal denoiser (ADR-0039): it filters the diffuse and the specular light of
/// the lighting pass, averaging each pixel with its history, then with its neighbours on the same surface, as
/// much as its noise asks. Light that is not noisy goes through unchanged. A concrete class: another
/// denoiser would take the same inputs and give the same outputs. The Device must outlive it.
class Denoiser {
public:
    static core::Result<Denoiser> Create(rhi::Device& device);

    Denoiser(Denoiser&& other) noexcept;
    Denoiser& operator=(Denoiser&& other) = delete;
    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;
    ~Denoiser();

    /// Follows the size of the frame. A new size drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Drops the history: the next frame is filtered alone.
    void Reset() { m_hasHistory = false; }
    /// Adds the passes that filter the frame's light, as "Denoiser". Valid after Resize.
    void AddTo(RenderGraph& graph, const DenoiserInputs& inputs);

    /// The filtered light of the last frame added, in the formats of its inputs, with its variance in alpha.
    [[nodiscard]] rhi::TextureHandle Diffuse() const { return m_diffuse.work[0]; }
    [[nodiscard]] rhi::TextureHandle Specular() const { return m_specular.work[0]; }

    /// Iterations of the spatial filter, each twice as wide as the last.
    static constexpr std::uint32_t kIterations = 5;

private:
    explicit Denoiser(rhi::Device& device);

    /// The textures of one kind of light.
    struct Signal {
        /// The average of the frames, unfiltered, which the next frame adds to; this frame's and the last.
        std::array<rhi::TextureHandle, 2> history;
        /// What the iterations of the spatial filter write in turn.
        std::array<rhi::TextureHandle, 2> work;
    };

    void DestroyTextures();

    rhi::Device* m_device;
    rhi::PipelineHandle m_temporal;
    rhi::PipelineHandle m_spatial;
    Signal m_diffuse;
    Signal m_specular;
    /// Linear depth, normal and frames averaged; this frame's and the last.
    std::array<rhi::TextureHandle, 2> m_guides;
    /// The moments of the light's luminance; this frame's and the last.
    std::array<rhi::TextureHandle, 2> m_moments;
    std::uint32_t m_current = 0;
    rhi::Extent m_extent;
    bool m_hasHistory = false;
};

} // namespace daidalos::renderer
