// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Error.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "FxaaPass.hpp"
#include "GpuFixture.hpp"

using namespace daidalos;

namespace {

constexpr rhi::Extent kExtent{32, 32};

// Runs FXAA over an image that is white where inside(x, y), black elsewhere, and returns the red channel
// of the result, row by row.
template <typename Inside>
std::vector<std::uint8_t> Antialias(rhi::Device& device, Inside inside) {
    std::vector<std::byte> pixels(std::size_t{kExtent.width} * kExtent.height * 4);
    for (std::uint32_t y = 0; y < kExtent.height; ++y) {
        for (std::uint32_t x = 0; x < kExtent.width; ++x) {
            // White, with a luma of 1, or black with 0.
            const auto value = inside(x, y) ? std::byte{255} : std::byte{0};
            for (std::size_t channel = 0; channel < 4; ++channel) {
                pixels[(std::size_t{y} * kExtent.width + x) * 4 + channel] = value;
            }
        }
    }
    core::Result<renderer::FxaaPass> pass = renderer::FxaaPass::Create(device, rhi::Format::Rgba8Srgb);
    EXPECT_TRUE(pass.has_value());
    const rhi::TextureHandle source =
        device
            .CreateTexture({.extent = kExtent,
                            .format = renderer::FxaaPass::kSourceFormat,
                            .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                            .name = "FXAA source"})
            .value_or(rhi::TextureHandle{});
    const rhi::TextureHandle target =
        device
            .CreateTexture({.extent = kExtent,
                            .format = rhi::Format::Rgba8Srgb,
                            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                            .name = "FXAA target"})
            .value_or(rhi::TextureHandle{});
    const rhi::BufferHandle readback = device
                                           .CreateBuffer({.size = pixels.size(),
                                                          .usage = rhi::BufferUsage::TransferDst,
                                                          .memory = rhi::MemoryLocation::GpuToCpu,
                                                          .name = "FXAA readback"})
                                           .value_or(rhi::BufferHandle{});
    EXPECT_TRUE(device.UploadTexture(source, pixels).has_value());
    EXPECT_TRUE(device
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Transition(source, rhi::TextureState::ShaderRead);
                        commands.Transition(target, rhi::TextureState::ColorTarget);
                        commands.BeginRendering({.colorTarget = target});
                        pass->Record(commands, source, kExtent);
                        commands.EndRendering();
                        commands.Transition(target, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(target, readback);
                    })
                    .has_value());
    EXPECT_TRUE(device.WaitIdle().has_value());
    std::vector<std::uint8_t> red(std::size_t{kExtent.width} * kExtent.height);
    const std::span<const std::byte> mapped = device.Mapped(readback);
    for (std::size_t pixel = 0; pixel < red.size(); ++pixel) {
        red[pixel] = static_cast<std::uint8_t>(mapped[pixel * 4]);
    }
    device.DestroyBuffer(readback);
    device.DestroyTexture(target);
    device.DestroyTexture(source);
    return red;
}

std::uint8_t At(const std::vector<std::uint8_t>& image, std::uint32_t x, std::uint32_t y) {
    return image[std::size_t{y} * kExtent.width + x];
}

class FxaaPassTest : public gpu_tests::GpuTest {};

} // namespace

TEST_F(FxaaPassTest, SmoothsAJaggedEdgeAndLeavesFlatAreasAlone) {
    // A shallow slope, one pixel up every four: the stairs FXAA is made for.
    const std::vector<std::uint8_t> image =
        Antialias(Device(), [](std::uint32_t x, std::uint32_t y) { return y * 4 < x + 32; });
    // Flat areas keep their color.
    EXPECT_EQ(At(image, 2, 1), 255);
    EXPECT_EQ(At(image, 30, 30), 0);
    // Along the edge, some pixels now lie between black and white.
    int blended = 0;
    for (std::uint32_t x = 4; x < 28; ++x) {
        for (std::uint32_t y = 8; y < 16; ++y) {
            const std::uint8_t value = At(image, x, y);
            blended += (value > 20 && value < 235) ? 1 : 0;
        }
    }
    EXPECT_GT(blended, 10);
}

TEST_F(FxaaPassTest, KeepsAnImageWithoutEdges) {
    const std::vector<std::uint8_t> image = Antialias(Device(), [](std::uint32_t, std::uint32_t) { return true; });
    for (const std::uint8_t value : image) {
        ASSERT_EQ(value, 255);
    }
}
