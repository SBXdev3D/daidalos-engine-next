// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ui/Canvas.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "GpuFixture.hpp"
#include "UiPass.hpp"

using namespace daidalos;

namespace {

constexpr rhi::Extent kExtent{32, 32};

using Pixels = std::vector<std::array<std::uint8_t, 4>>;

// Draws the canvas over black into an sRGB target, and reads it back.
Pixels Draw(rhi::Device& device, const ui::Canvas& canvas) {
    core::Result<renderer::UiPass> pass = renderer::UiPass::Create(device, rhi::Format::Rgba8Srgb);
    EXPECT_TRUE(pass.has_value());
    const rhi::TextureHandle target =
        device
            .CreateTexture({.extent = kExtent,
                            .format = rhi::Format::Rgba8Srgb,
                            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                            .name = "Interface target"})
            .value_or(rhi::TextureHandle{});
    const rhi::BufferHandle readback = device
                                           .CreateBuffer({.size = std::size_t{kExtent.width} * kExtent.height * 4,
                                                          .usage = rhi::BufferUsage::TransferDst,
                                                          .memory = rhi::MemoryLocation::GpuToCpu,
                                                          .name = "Interface readback"})
                                           .value_or(rhi::BufferHandle{});
    core::Result<void> recorded;
    EXPECT_TRUE(device
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Transition(target, rhi::TextureState::ColorTarget);
                        commands.BeginRendering(
                            {.colorTarget = target, .clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)});
                        recorded = pass->Record(commands, canvas);
                        commands.EndRendering();
                        commands.Transition(target, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(target, readback);
                    })
                    .has_value());
    EXPECT_TRUE(recorded.has_value());
    EXPECT_TRUE(device.WaitIdle().has_value());
    Pixels pixels(std::size_t{kExtent.width} * kExtent.height);
    const std::span<const std::byte> mapped = device.Mapped(readback);
    for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            pixels[pixel][channel] = static_cast<std::uint8_t>(mapped[(pixel * 4) + channel]);
        }
    }
    device.DestroyBuffer(readback);
    device.DestroyTexture(target);
    return pixels;
}

std::array<std::uint8_t, 4> At(const Pixels& pixels, std::uint32_t x, std::uint32_t y) {
    return pixels[(std::size_t{y} * kExtent.width) + x];
}

rhi::TextureHandle Texture(rhi::Device& device, rhi::Format format, rhi::Extent extent,
                           std::span<const std::byte> data) {
    const rhi::TextureHandle texture =
        device
            .CreateTexture({.extent = extent,
                            .format = format,
                            .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                            .name = "Interface test texture"})
            .value_or(rhi::TextureHandle{});
    EXPECT_TRUE(device.UploadTexture(texture, data).has_value());
    return texture;
}

class UiPassTest : public gpu_tests::GpuTest {};

} // namespace

TEST_F(UiPassTest, FillsRectanglesInSrgbColors) {
    ui::Canvas canvas;
    canvas.Begin(kExtent);
    // The left half of the 1080-unit-wide canvas, in red, and a gray at the bottom right.
    canvas.FillRect({{0.0f, 0.0f}, {540.0f, 1080.0f}}, {1.0f, 0.0f, 0.0f, 1.0f});
    canvas.FillRect({{540.0f, 540.0f}, {540.0f, 540.0f}}, {0.5f, 0.5f, 0.5f, 1.0f});
    const Pixels pixels = Draw(Device(), canvas);

    EXPECT_EQ(At(pixels, 4, 4), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(At(pixels, 28, 4), (std::array<std::uint8_t, 4>{0, 0, 0, 255}));
    // The sRGB value comes back as it was given: 0.5 is 128.
    EXPECT_NEAR(At(pixels, 28, 28)[0], 128, 1);
    EXPECT_EQ(Device().ValidationErrorCount(), 0U);
}

TEST_F(UiPassTest, ImagesAreMultipliedByTheirColorAndBlendedByAlpha) {
    std::vector<std::byte> white(4 * 4 * 4, std::byte{255});
    const rhi::TextureHandle texture = Texture(Device(), rhi::Format::Rgba8Srgb, {4, 4}, white);
    ui::Canvas canvas;
    canvas.Begin(kExtent);
    canvas.DrawImage(texture, {{0.0f, 0.0f}, {1080.0f, 540.0f}}, {.color = {0.0f, 1.0f, 0.0f, 1.0f}});
    // Half transparent white over black: half the light, which sRGB stores as about 188.
    canvas.DrawImage(texture, {{0.0f, 540.0f}, {1080.0f, 540.0f}}, {.color = {1.0f, 1.0f, 1.0f, 0.5f}});
    const Pixels pixels = Draw(Device(), canvas);

    EXPECT_EQ(At(pixels, 16, 4), (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
    EXPECT_NEAR(At(pixels, 16, 28)[0], 188, 2);
    Device().DestroyTexture(texture);
    EXPECT_EQ(Device().ValidationErrorCount(), 0U);
}

TEST_F(UiPassTest, TextCoversWhereTheDistanceFieldIsInside) {
    // A 2 by 1 atlas: inside on the left, outside on the right. At 10.8 times the bake size, 'I' covers
    // the left half of the canvas with the left texel, and 'O' the right half with the right one.
    const std::array<std::byte, 2> atlas{std::byte{255}, std::byte{0}};
    ui::Font font;
    font.atlas = Texture(Device(), rhi::Format::R8Unorm, {2, 1}, atlas);
    font.data.size = 100.0f;
    font.data.ascent = 100.0f;
    font.data.glyphs = {
        {.codepoint = U'I',
         .advance = 50.0f,
         .left = 0.0f,
         .top = -100.0f,
         .width = 50.0f,
         .height = 100.0f,
         .u0 = 0.0f,
         .v0 = 0.0f,
         .u1 = 0.25f,
         .v1 = 1.0f},
        {.codepoint = U'O',
         .advance = 50.0f,
         .left = 0.0f,
         .top = -100.0f,
         .width = 50.0f,
         .height = 100.0f,
         .u0 = 0.75f,
         .v0 = 0.0f,
         .u1 = 1.0f,
         .v1 = 1.0f},
    };
    ui::Canvas canvas;
    canvas.Begin(kExtent);
    canvas.DrawText(font, "IO", {0.0f, 0.0f}, {.size = 1080.0f, .color = glm::vec4(1.0f)});
    const Pixels pixels = Draw(Device(), canvas);

    EXPECT_EQ(At(pixels, 4, 16)[0], 255);
    EXPECT_EQ(At(pixels, 28, 16)[0], 0);
    Device().DestroyTexture(font.atlas);
    EXPECT_EQ(Device().ValidationErrorCount(), 0U);
}
