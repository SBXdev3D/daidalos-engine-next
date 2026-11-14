// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Exposure.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/ExposureHistogram.cs.spv.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches HistogramPushConstants in shaders/ExposureHistogram.slang.
struct HistogramPushConstants {
    std::uint32_t image;
    std::uint32_t histogram;
    std::uint32_t width;
    std::uint32_t height;
    float toNits;
    std::uint32_t clear;
};

constexpr std::uint32_t kGroupSize = 16;
constexpr std::uint64_t kHistogramBytes = kHistogramBins * sizeof(std::uint32_t);
/// log2(100 / 12.5): from a mean luminance in nits to EV100, with the reflected-light meter's constant.
constexpr float kMeterOffset = 3.0f;

/// The log2 luminance a bin stands for: its middle, or the lowest for bin 0.
float BinLog2(std::uint32_t bin) {
    if (bin == 0) {
        return kHistogramLowest;
    }
    constexpr float kStep = (kHistogramHighest - kHistogramLowest) / static_cast<float>(kHistogramBins - 1);
    return kHistogramLowest + ((static_cast<float>(bin) - 0.5f) * kStep);
}

} // namespace

std::optional<float> MeterEv100(std::span<const std::uint32_t> bins, float lowFraction, float highFraction) {
    double total = 0.0;
    for (const std::uint32_t weight : bins) {
        total += weight;
    }
    if (total <= 0.0) {
        return std::nullopt;
    }
    // The weight from low to high, bin by bin, counting the part of each bin that lies between them.
    const double low = total * lowFraction;
    const double high = total * std::max(highFraction, lowFraction);
    double below = 0.0;
    double sum = 0.0;
    double kept = 0.0;
    for (std::uint32_t bin = 0; bin < bins.size(); ++bin) {
        const double start = below;
        below += bins[bin];
        const double inside = std::min(below, high) - std::max(start, low);
        if (inside > 0.0) {
            sum += inside * BinLog2(bin);
            kept += inside;
        }
    }
    if (kept <= 0.0) {
        // The two fractions are equal: the bin where they fall holds the answer.
        double reached = 0.0;
        for (std::uint32_t bin = 0; bin < bins.size(); ++bin) {
            reached += bins[bin];
            if (reached >= low) {
                return BinLog2(bin) + kMeterOffset;
            }
        }
        return std::nullopt;
    }
    return static_cast<float>(sum / kept) + kMeterOffset;
}

float EyeAdaptation::Update(std::optional<float> metered, float seconds, float lowest, float highest) {
    if (metered.has_value() && seconds > 0.0f) {
        const float target = std::clamp(*metered, lowest, highest);
        // A higher EV100 is brighter light: the eye closes fast and opens slowly.
        const float timeConstant = target > m_ev100 ? kBrightSeconds : kDarkSeconds;
        m_ev100 += (target - m_ev100) * (1.0f - std::exp(-seconds / timeConstant));
    }
    m_ev100 = std::clamp(m_ev100, lowest, highest);
    m_started = true;
    return m_ev100;
}

ExposureMeter::ExposureMeter(rhi::Device& device) : m_device(&device) {}

ExposureMeter::ExposureMeter(ExposureMeter&& other) noexcept
    : m_device(other.m_device), m_pipeline(std::exchange(other.m_pipeline, {})),
      m_histogram(std::exchange(other.m_histogram, {})), m_readback(std::exchange(other.m_readback, {})),
      m_written(other.m_written), m_read(std::move(other.m_read)) {}

ExposureMeter::~ExposureMeter() {
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyBuffer(m_histogram);
    for (const rhi::BufferHandle buffer : m_readback) {
        m_device->DestroyBuffer(buffer);
    }
}

core::Result<ExposureMeter> ExposureMeter::Create(rhi::Device& device) {
    ExposureMeter meter(device);
    DAI_TRY_ASSIGN(meter.m_pipeline, device.CreateComputePipeline(
                                         {.shader = rhi::ToSpirv(kExposureHistogramCsSpirv), .name = "Exposure"}));
    DAI_TRY_ASSIGN(meter.m_histogram,
                   device.CreateBuffer({.size = kHistogramBytes,
                                        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferSrc,
                                        .name = "Exposure histogram"}));
    for (rhi::BufferHandle& buffer : meter.m_readback) {
        DAI_TRY_ASSIGN(buffer, device.CreateBuffer({.size = kHistogramBytes,
                                                    .usage = rhi::BufferUsage::TransferDst,
                                                    .memory = rhi::MemoryLocation::GpuToCpu,
                                                    .name = "Exposure readback"}));
    }
    meter.m_read.resize(kHistogramBins);
    return meter;
}

std::span<const std::uint32_t> ExposureMeter::Read(std::uint32_t slot) {
    if (!m_written[slot]) {
        return {};
    }
    m_written[slot] = false;
    const std::span<const std::byte> mapped = m_device->Mapped(m_readback[slot]);
    std::memcpy(m_read.data(), mapped.data(), kHistogramBytes);
    return m_read;
}

void ExposureMeter::AddTo(RenderGraph& graph, std::uint32_t slot, rhi::TextureHandle image, rhi::Extent extent,
                          float preExposure) {
    const HistogramPushConstants push{.image = m_device->BindlessIndex(image),
                                      .histogram = m_device->StorageIndex(m_histogram),
                                      .width = extent.width,
                                      .height = extent.height,
                                      .toNits = 1.0f / preExposure,
                                      .clear = 0};
    graph.AddPass({.name = "Exposure",
                   .textures = {{image, rhi::Access::ComputeShaderRead}},
                   .buffers = {{m_histogram, rhi::Access::ComputeShaderWrite}}},
                  [this, push](rhi::CommandList& commands) {
                      DAI_PROFILE_SCOPE("renderer::Exposure");
                      commands.BindPipeline(m_pipeline);
                      HistogramPushConstants clear = push;
                      clear.clear = 1;
                      commands.Push(clear);
                      commands.Dispatch(1);
                      commands.Barrier();
                      commands.Push(push);
                      commands.Dispatch((push.width + kGroupSize - 1) / kGroupSize,
                                        (push.height + kGroupSize - 1) / kGroupSize);
                  });
    const rhi::BufferHandle readback = m_readback[slot];
    graph.AddPass(
        {.name = "Exposure readback",
         .group = "Exposure",
         .buffers = {{m_histogram, rhi::Access::TransferRead}, {readback, rhi::Access::TransferWrite}}},
        [this, readback](rhi::CommandList& commands) { commands.CopyBuffer(m_histogram, readback, kHistogramBytes); });
    m_written[slot] = true;
}

} // namespace daidalos::renderer
