// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "GpuFixture.hpp"

using namespace daidalos;
using gpu_tests::GpuTest;

namespace {

void SetEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

} // namespace

TEST_F(GpuTest, ReportsTheSelectedDevice) {
    EXPECT_FALSE(Device().Info().name.empty());
}

TEST_F(GpuTest, OnlyOneDeviceCanExist) {
    const core::Result<rhi::Device> second = rhi::Device::Create({});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, core::ErrorCode::InvalidState);
}

TEST_F(GpuTest, BufferDataSurvivesARoundTripThroughGpuMemory) {
    const std::array<std::uint32_t, 4> data{1, 2, 3, 0xDEADBEEF};
    const auto bytes = std::as_bytes(std::span(data));

    const core::Result<rhi::BufferHandle> gpu = Device().CreateBuffer(
        {.size = bytes.size(), .usage = rhi::BufferUsage::TransferSrc | rhi::BufferUsage::TransferDst, .name = "Gpu"});
    ASSERT_TRUE(gpu.has_value());
    const core::Result<rhi::BufferHandle> readback = Device().CreateBuffer({.size = bytes.size(),
                                                                            .usage = rhi::BufferUsage::TransferDst,
                                                                            .memory = rhi::MemoryLocation::GpuToCpu,
                                                                            .name = "Readback"});
    ASSERT_TRUE(readback.has_value());

    ASSERT_TRUE(Device().UploadBuffer(*gpu, bytes).has_value());
    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) { commands.CopyBuffer(*gpu, *readback, bytes.size()); })
                    .has_value());

    EXPECT_EQ(std::memcmp(Device().Mapped(*readback).data(), data.data(), bytes.size()), 0);
    Device().DestroyBuffer(*gpu);
    Device().DestroyBuffer(*readback);
}

TEST_F(GpuTest, TextureDataSurvivesARoundTripThroughGpuMemory) {
    const std::array<std::uint32_t, 4> pixels{0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0x80808080};
    const auto bytes = std::as_bytes(std::span(pixels));

    const core::Result<rhi::TextureHandle> texture = Device().CreateTexture(
        {.extent = {2, 2},
         .format = rhi::Format::Rgba8Unorm,
         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc,
         .name = "Texture"});
    ASSERT_TRUE(texture.has_value());
    const core::Result<rhi::BufferHandle> readback = Device().CreateBuffer({.size = bytes.size(),
                                                                            .usage = rhi::BufferUsage::TransferDst,
                                                                            .memory = rhi::MemoryLocation::GpuToCpu,
                                                                            .name = "Readback"});
    ASSERT_TRUE(readback.has_value());

    ASSERT_TRUE(Device().UploadTexture(*texture, bytes).has_value());
    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Transition(*texture, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(*texture, *readback);
                    })
                    .has_value());

    EXPECT_EQ(std::memcmp(Device().Mapped(*readback).data(), pixels.data(), bytes.size()), 0);
    Device().DestroyTexture(*texture);
    Device().DestroyBuffer(*readback);
}

TEST_F(GpuTest, DestroyedHandleBecomesStaleImmediately) {
    const core::Result<rhi::BufferHandle> buffer =
        Device().CreateBuffer({.size = 16, .usage = rhi::BufferUsage::Vertex, .name = "Buffer"});
    ASSERT_TRUE(buffer.has_value());
    Device().DestroyBuffer(*buffer);
    Device().DestroyBuffer(*buffer); // A second destruction does nothing.
    EXPECT_EQ(Device().PendingDestructionCount(), 1U);
}

TEST_F(GpuTest, DestructionWaitsForTheFrameThatUsesTheResource) {
    const core::Result<rhi::BufferHandle> buffer =
        Device().CreateBuffer({.size = 16, .usage = rhi::BufferUsage::TransferSrc, .name = "Buffer"});
    ASSERT_TRUE(buffer.has_value());

    core::Result<std::optional<rhi::Frame>> frame = Device().BeginFrame();
    ASSERT_TRUE(frame.has_value() && frame->has_value());
    Device().DestroyBuffer(*buffer);
    EXPECT_EQ(Device().PendingDestructionCount(), 1U);
    ASSERT_TRUE(Device().EndFrame().has_value());

    // The buffer goes away once the GPU has passed the frame above.
    ASSERT_TRUE(Device().WaitIdle().has_value());
    EXPECT_EQ(Device().PendingDestructionCount(), 0U);
}

TEST_F(GpuTest, FramesRunLongerThanTheFramesInFlight) {
    for (std::uint32_t frameIndex = 0; frameIndex < rhi::kFramesInFlight * 4; ++frameIndex) {
        core::Result<std::optional<rhi::Frame>> frame = Device().BeginFrame();
        ASSERT_TRUE(frame.has_value() && frame->has_value());
        ASSERT_TRUE(Device().EndFrame().has_value());
    }
}

TEST_F(GpuTest, BindlessSlotIsNotReusedBeforeTheGpuIsDone) {
    const rhi::TextureDesc desc{
        .extent = {1, 1}, .format = rhi::Format::Rgba8Unorm, .usage = rhi::TextureUsage::Sampled};
    const core::Result<rhi::TextureHandle> first = Device().CreateTexture(desc);
    ASSERT_TRUE(first.has_value());
    const std::uint32_t firstIndex = Device().BindlessIndex(*first);
    Device().DestroyTexture(*first);

    const core::Result<rhi::TextureHandle> second = Device().CreateTexture(desc);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(Device().BindlessIndex(*second), firstIndex);
    Device().DestroyTexture(*second);
}

TEST(DeviceLifetime, CanBeCreatedAndDestroyedRepeatedly) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        rhi::DeviceConfig config;
        config.validation = true;
        core::Result<rhi::Device> device = rhi::Device::Create(config);
        if (!device) {
            GTEST_SKIP() << core::Format(device.error());
        }
        const core::Result<rhi::BufferHandle> leaked =
            device->CreateBuffer({.size = 16, .usage = rhi::BufferUsage::Vertex, .name = "Leaked"});
        EXPECT_TRUE(leaked.has_value());
        EXPECT_EQ(device->ValidationErrorCount(), 0U);
    }
}

TEST(DeviceLifetime, MissingDriverIsReportedAsAnError) {
    SetEnvironment("VK_DRIVER_FILES", "daidalos-no-such-driver.json");
    const core::Result<rhi::Device> device = rhi::Device::Create({});
    SetEnvironment("VK_DRIVER_FILES", "");

    ASSERT_FALSE(device.has_value());
    EXPECT_EQ(device.error().code, core::ErrorCode::NotSupported);
    EXPECT_FALSE(device.error().message.empty());
}
