// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec3.hpp>

#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// What the path tracer is given each frame: storage indices of the frame's data, and the structures and
/// buffers its rays read.
struct PathTracerInputs {
    std::uint32_t frameBuffer = 0;
    std::uint32_t instanceBuffer = 0;
    std::uint32_t materialBuffer = 0;
    rhi::AccelerationStructureHandle structure;
    /// The frame's skinned vertices, or null.
    rhi::BufferHandle skinnedVertices;
};

struct PathTracerSettings {
    /// Surfaces a path meets at most: 1 is direct light only.
    std::uint32_t maxBounces = 4;
    /// Paths per pixel after which the image stops changing.
    std::uint32_t maxSamples = 4096;
    /// Radiance of whatever a path that leaves the scene would see.
    glm::vec3 background{0.0f};
    /// Changes the random numbers, for the same image with other noise.
    std::uint32_t seed = 0;
    /// Spreads the paths over each pixel, which smooths edges. Without it, every path goes through the
    /// pixel's center, as the mesh pass shades it: what tests compare.
    bool jitter = true;
};

/// The reference path tracer (ADR-0034): one path per pixel per frame, into an image that averages the
/// paths since the last Reset. The Device must outlive it.
class PathTracerPass {
public:
    static core::Result<PathTracerPass> Create(rhi::Device& device);

    PathTracerPass(PathTracerPass&& other) noexcept;
    PathTracerPass& operator=(PathTracerPass&& other) = delete;
    PathTracerPass(const PathTracerPass&) = delete;
    PathTracerPass& operator=(const PathTracerPass&) = delete;
    ~PathTracerPass();

    /// Follows the size of the frame. A new size starts the average over.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Starts the average over, from the next frame: the view or the scene changed.
    void Reset() { m_samples = 0; }
    /// Adds the pass that traces one more path per pixel, as "Path tracer", unless the image holds
    /// maxSamples already. Must come after the frame's acceleration structure is built.
    void AddTo(RenderGraph& graph, const PathTracerInputs& inputs, const PathTracerSettings& settings);

    /// The average radiance of the paths so far, in linear HDR. Valid after Resize.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_output; }
    /// Paths averaged in Output.
    [[nodiscard]] std::uint32_t Samples() const { return m_samples; }

private:
    explicit PathTracerPass(rhi::Device& device);

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    rhi::TextureHandle m_output;
    rhi::Extent m_extent;
    std::uint32_t m_samples = 0;
};

} // namespace daidalos::renderer
