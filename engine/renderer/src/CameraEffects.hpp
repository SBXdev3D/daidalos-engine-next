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

/// What the camera's lens and shutter do to an image (ADR-0063).
struct CameraEffectsInputs {
    /// The frame's FrameData, for its depth's planes and the corner the frame draws into.
    std::uint32_t frameBuffer = 0;
    /// The antialiased image, at the output's size, and the G-buffer's depth and motion vectors.
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    rhi::TextureHandle motion;
    /// Depth of field: the lens's f-number (0 for none), the focus distance in meters, the vertical field of
    /// view in radians, on a 24 mm high sensor.
    float aperture = 0.0f;
    float focusDistance = 0.0f;
    float verticalFov = 1.0f;
    /// Motion blur: the fraction of the frame's time the shutter is open, 0 for none.
    float shutter = 0.0f;
};

/// Pixels a point blurs into, per unit of |depth - focus| / depth, for a lens of this f-number focused at
/// this distance, with this vertical field of view on a 24 mm high sensor, in an image this many pixels high.
[[nodiscard]] float CircleOfConfusionScale(float aperture, float focusDistance, float verticalFov,
                                           std::uint32_t imageHeight);

class CameraEffects {
public:
    static core::Result<CameraEffects> Create(rhi::Device& device);

    CameraEffects(CameraEffects&& other) noexcept;
    CameraEffects& operator=(CameraEffects&& other) = delete;
    CameraEffects(const CameraEffects&) = delete;
    CameraEffects& operator=(const CameraEffects&) = delete;
    ~CameraEffects();

    /// Follows the size of the image.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Adds depth of field, then motion blur, as "Camera effects", and returns the image with them: the input
    /// itself when neither applies. Valid after Resize.
    [[nodiscard]] rhi::TextureHandle AddTo(RenderGraph& graph, const CameraEffectsInputs& inputs);

    /// The widest blur of depth of field, in pixels.
    static constexpr float kMaxCoc = 24.0f;

private:
    explicit CameraEffects(rhi::Device& device) : m_device(&device) {}
    void DestroyImages();

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    std::array<rhi::TextureHandle, 2> m_images{};
    rhi::Extent m_extent;
};

} // namespace daidalos::renderer
