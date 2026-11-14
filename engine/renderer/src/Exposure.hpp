// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace daidalos::renderer {

class RenderGraph;

/// Auto-exposure (ADR-0048): the GPU meters each frame into a histogram of luminance, which the CPU reads
/// back a few frames later and moves the eye towards, within the range the level sets.

/// Bins of the histogram. Bin 0 holds black and what is darker than kHistogramLowest; the others split
/// log2 luminance, in nits, evenly from kHistogramLowest to kHistogramHighest.
inline constexpr std::uint32_t kHistogramBins = 128;
inline constexpr float kHistogramLowest = -10.0f;
inline constexpr float kHistogramHighest = 16.0f;

/// What the histogram's pixels average to, as an exposure value at ISO 100: the mean log2 luminance of the
/// weight between two fractions of it, from the darkest, so that black corners and a few bright lamps do not
/// decide, plus log2(100 / 12.5), photography's reflected-light meter. Nothing for an empty histogram.
[[nodiscard]] std::optional<float> MeterEv100(std::span<const std::uint32_t> bins, float lowFraction = 0.4f,
                                              float highFraction = 0.95f);

/// The eye's exposure, which follows the metered light: fast towards bright light, slowly into the dark,
/// as eyes do, and never outside the level's range.
class EyeAdaptation {
public:
    /// Seconds for the eye to go about 63% of the way: towards brighter light, and into the dark.
    static constexpr float kBrightSeconds = 0.5f;
    static constexpr float kDarkSeconds = 3.0f;

    /// The eye is there at once.
    void Reset(float ev100) {
        m_ev100 = ev100;
        m_started = true;
    }
    /// Moves towards the metered exposure over this many seconds, clamped to the range; without a
    /// metering, stays where it is, clamped. Returns the eye's EV100.
    float Update(std::optional<float> metered, float seconds, float lowest, float highest);

    [[nodiscard]] bool Started() const { return m_started; }
    [[nodiscard]] float Ev100() const { return m_ev100; }

private:
    float m_ev100 = 0.0f;
    bool m_started = false;
};

/// Builds the histogram of a frame on the GPU and reads it back when the frame's slot comes back. The
/// Device must outlive it.
class ExposureMeter {
public:
    static core::Result<ExposureMeter> Create(rhi::Device& device);

    ExposureMeter(ExposureMeter&& other) noexcept;
    ExposureMeter& operator=(ExposureMeter&& other) = delete;
    ExposureMeter(const ExposureMeter&) = delete;
    ExposureMeter& operator=(const ExposureMeter&) = delete;
    ~ExposureMeter();

    /// The histogram of the last frame recorded in this slot, which the GPU has finished: call after
    /// BeginFrame. Empty if that frame was not metered.
    [[nodiscard]] std::span<const std::uint32_t> Read(std::uint32_t slot);
    /// Adds the pass that meters an HDR image, exposed by preExposure (what a nit was multiplied by), as
    /// "Exposure", and copies the histogram for the CPU.
    void AddTo(RenderGraph& graph, std::uint32_t slot, rhi::TextureHandle image, rhi::Extent extent, float preExposure);

private:
    explicit ExposureMeter(rhi::Device& device);

    rhi::Device* m_device;
    rhi::PipelineHandle m_pipeline;
    rhi::BufferHandle m_histogram;
    std::array<rhi::BufferHandle, rhi::kFramesInFlight> m_readback{};
    std::array<bool, rhi::kFramesInFlight> m_written{};
    std::vector<std::uint32_t> m_read;
};

} // namespace daidalos::renderer
