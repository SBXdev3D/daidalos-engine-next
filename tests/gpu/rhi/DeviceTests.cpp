// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/rhi/Spirv.hpp>

#include <shaders/ComputeTest.cs.spv.h>
#include <shaders/HalfTriangleTest.ps.spv.h>
#include <shaders/HalfTriangleTest.vs.spv.h>
#include <shaders/TriangleTest.ps.spv.h>
#include <shaders/TriangleTest.vs.spv.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
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

TEST_F(GpuTest, UploadsReachTheGpuInOrderBeforeTheNextFrame) {
    const std::array<std::uint32_t, 2> first{1, 2};
    const std::array<std::uint32_t, 2> second{3, 4};
    const std::array<std::uint32_t, 2> other{5, 6};
    const auto size = std::as_bytes(std::span(first)).size();
    const auto create = [&](rhi::BufferUsage usage, rhi::MemoryLocation memory) {
        core::Result<rhi::BufferHandle> buffer =
            Device().CreateBuffer({.size = size, .usage = usage, .memory = memory});
        EXPECT_TRUE(buffer.has_value());
        return buffer.value_or(rhi::BufferHandle{});
    };
    const rhi::BufferHandle a =
        create(rhi::BufferUsage::TransferSrc | rhi::BufferUsage::TransferDst, rhi::MemoryLocation::GpuOnly);
    const rhi::BufferHandle b =
        create(rhi::BufferUsage::TransferSrc | rhi::BufferUsage::TransferDst, rhi::MemoryLocation::GpuOnly);
    const rhi::BufferHandle readback = create(rhi::BufferUsage::TransferDst, rhi::MemoryLocation::GpuToCpu);
    const rhi::BufferHandle readbackOther = create(rhi::BufferUsage::TransferDst, rhi::MemoryLocation::GpuToCpu);

    // Three uploads with no submission in between: the second one to buffer a overwrites the first.
    ASSERT_TRUE(Device().UploadBuffer(a, std::as_bytes(std::span(first))).has_value());
    ASSERT_TRUE(Device().UploadBuffer(b, std::as_bytes(std::span(other))).has_value());
    ASSERT_TRUE(Device().UploadBuffer(a, std::as_bytes(std::span(second))).has_value());

    core::Result<std::optional<rhi::Frame>> frame = Device().BeginFrame();
    ASSERT_TRUE(frame.has_value() && frame->has_value());
    (*frame)->commands.CopyBuffer(a, readback, size);
    (*frame)->commands.CopyBuffer(b, readbackOther, size);
    ASSERT_TRUE(Device().EndFrame().has_value());
    ASSERT_TRUE(Device().WaitIdle().has_value());

    EXPECT_EQ(std::memcmp(Device().Mapped(readback).data(), second.data(), size), 0);
    EXPECT_EQ(std::memcmp(Device().Mapped(readbackOther).data(), other.data(), size), 0);
    for (const rhi::BufferHandle buffer : {a, b, readback, readbackOther}) {
        Device().DestroyBuffer(buffer);
    }
}

TEST_F(GpuTest, BufferDestroyedBeforeItsUploadIsSentIsReleasedSafely) {
    const std::array<std::uint32_t, 4> data{1, 2, 3, 4};
    const auto bytes = std::as_bytes(std::span(data));
    const core::Result<rhi::BufferHandle> buffer =
        Device().CreateBuffer({.size = bytes.size(), .usage = rhi::BufferUsage::TransferDst});
    ASSERT_TRUE(buffer.has_value());

    ASSERT_TRUE(Device().UploadBuffer(*buffer, bytes).has_value());
    Device().DestroyBuffer(*buffer);
    EXPECT_GT(Device().PendingDestructionCount(), 0U);

    ASSERT_TRUE(Device().WaitIdle().has_value());
    EXPECT_EQ(Device().PendingDestructionCount(), 0U);
}

TEST_F(GpuTest, TimestampsOfAFrameComeBackInOrder) {
    const auto frame = [this](int timestamps) {
        core::Result<std::optional<rhi::Frame>> begun = Device().BeginFrame();
        ASSERT_TRUE(begun.has_value() && begun->has_value());
        for (int index = 0; index < timestamps; ++index) {
            (*begun)->commands.WriteTimestamp();
        }
        ASSERT_TRUE(Device().EndFrame().has_value());
    };
    frame(3);
    for (std::uint32_t index = 1; index < rhi::kFramesInFlight; ++index) {
        frame(0);
    }
    // The next BeginFrame reuses the first frame's slot, and reads its timestamps.
    frame(0);

    const std::span<const double> timestamps = Device().CompletedTimestamps();
    if (timestamps.empty()) {
        GTEST_SKIP() << "This device has no timestamps";
    }
    ASSERT_EQ(timestamps.size(), 3U);
    EXPECT_EQ(timestamps[0], 0.0);
    EXPECT_GE(timestamps[1], timestamps[0]);
    EXPECT_GE(timestamps[2], timestamps[1]);
}

TEST_F(GpuTest, TimestampsAreRefusedOutsideAFrame) {
    bool written = true;
    ASSERT_TRUE(Device().Submit([&](rhi::CommandList& commands) { written = commands.WriteTimestamp(); }).has_value());
    EXPECT_FALSE(written);
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

TEST(TextureSizes, MipExtentsHalveAndStopAtOne) {
    EXPECT_EQ(rhi::MipExtent({8, 3}, 0), (rhi::Extent{8, 3}));
    EXPECT_EQ(rhi::MipExtent({8, 3}, 1), (rhi::Extent{4, 1}));
    EXPECT_EQ(rhi::MipExtent({8, 3}, 3), (rhi::Extent{1, 1}));
}

TEST(TextureSizes, BlockFormatsRoundUpToWholeBlocks) {
    EXPECT_EQ(rhi::MipByteSize(rhi::Format::Rgba8Srgb, {3, 2}), 24U);
    EXPECT_EQ(rhi::MipByteSize(rhi::Format::Bc7Srgb, {4, 4}), 16U);
    EXPECT_EQ(rhi::MipByteSize(rhi::Format::Bc7Unorm, {5, 1}), 32U);
    EXPECT_EQ(rhi::MipByteSize(rhi::Format::Bc5Unorm, {1, 1}), 16U);
}

TEST_F(GpuTest, EveryMipLevelSurvivesARoundTripThroughGpuMemory) {
    // Levels of 4x2, 2x1 and 1x1 pixels.
    const std::array<std::uint32_t, 11> chain{10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 30};
    const auto bytes = std::as_bytes(std::span(chain));

    const core::Result<rhi::TextureHandle> texture = Device().CreateTexture(
        {.extent = {4, 2},
         .format = rhi::Format::Rgba8Unorm,
         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc,
         .mipLevels = 3,
         .name = "Mips"});
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
                        commands.CopyTextureToBuffer(*texture, *readback, 1);
                    })
                    .has_value());
    std::array<std::uint32_t, 2> level1{};
    std::memcpy(level1.data(), Device().Mapped(*readback).data(), sizeof(level1));
    EXPECT_EQ(level1, (std::array<std::uint32_t, 2>{20, 21}));

    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) { commands.CopyTextureToBuffer(*texture, *readback, 2); })
                    .has_value());
    std::uint32_t level2 = 0;
    std::memcpy(&level2, Device().Mapped(*readback).data(), sizeof(level2));
    EXPECT_EQ(level2, 30U);

    Device().DestroyTexture(*texture);
    Device().DestroyBuffer(*readback);
}

TEST_F(GpuTest, BlockCompressedTextureCanBeUploaded) {
    const std::array<std::byte, 32> blocks{}; // Two levels of one block: a 4x4 and a 2x2 texture.
    const core::Result<rhi::TextureHandle> texture =
        Device().CreateTexture({.extent = {4, 4},
                                .format = rhi::Format::Bc7Srgb,
                                .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                                .mipLevels = 2,
                                .name = "Compressed"});
    ASSERT_TRUE(texture.has_value());
    EXPECT_TRUE(Device().UploadTexture(*texture, blocks).has_value());
    Device().DestroyTexture(*texture);
}

TEST_F(GpuTest, StorageSlotIsNotReusedBeforeTheGpuIsDone) {
    const rhi::BufferDesc desc{.size = 16, .usage = rhi::BufferUsage::Storage, .name = "Storage"};
    const core::Result<rhi::BufferHandle> first = Device().CreateBuffer(desc);
    ASSERT_TRUE(first.has_value());
    const std::uint32_t slot = Device().StorageIndex(*first);
    Device().DestroyBuffer(*first);

    const core::Result<rhi::BufferHandle> second = Device().CreateBuffer(desc);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(Device().StorageIndex(*second), slot);
    Device().DestroyBuffer(*second);
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

namespace {

struct ComputeTestPush {
    std::uint32_t values;
    std::uint32_t arguments;
    std::uint32_t count;
};

} // namespace

TEST_F(GpuTest, ComputeShaderWritesAStorageBuffer) {
    constexpr std::uint32_t kCount = 100;
    const core::Result<rhi::BufferHandle> values = Device().CreateBuffer({.size = kCount * sizeof(std::uint32_t),
                                                                          .usage = rhi::BufferUsage::Storage,
                                                                          .memory = rhi::MemoryLocation::GpuToCpu,
                                                                          .name = "Values"});
    const core::Result<rhi::BufferHandle> arguments = Device().CreateBuffer(
        {.size = 16, .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect, .name = "Arguments"});
    const core::Result<rhi::PipelineHandle> pipeline =
        Device().CreateComputePipeline({.shader = rhi::ToSpirv(kComputeTestCsSpirv), .name = "Compute test"});
    ASSERT_TRUE(values.has_value() && arguments.has_value());
    ASSERT_TRUE(pipeline.has_value()) << core::Format(pipeline.error());

    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        commands.BindPipeline(*pipeline);
                        commands.Push(
                            ComputeTestPush{Device().StorageIndex(*values), Device().StorageIndex(*arguments), kCount});
                        commands.Dispatch((kCount + 63) / 64);
                    })
                    .has_value());

    const auto* written = reinterpret_cast<const std::uint32_t*>(Device().Mapped(*values).data());
    for (std::uint32_t index = 0; index < kCount; ++index) {
        ASSERT_EQ(written[index], index * 2 + 1) << "at " << index;
    }
    Device().DestroyPipeline(*pipeline);
    Device().DestroyBuffer(*values);
    Device().DestroyBuffer(*arguments);
}

TEST_F(GpuTest, IndirectDrawUsesTheArgumentsTheGpuWrote) {
    constexpr rhi::Extent kExtent{8, 8};
    const core::Result<rhi::BufferHandle> values =
        Device().CreateBuffer({.size = sizeof(std::uint32_t), .usage = rhi::BufferUsage::Storage, .name = "Values"});
    const core::Result<rhi::BufferHandle> arguments = Device().CreateBuffer(
        {.size = 16, .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect, .name = "Arguments"});
    const core::Result<rhi::TextureHandle> target =
        Device().CreateTexture({.extent = kExtent,
                                .format = rhi::Format::Rgba8Unorm,
                                .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                .name = "Target"});
    const core::Result<rhi::BufferHandle> readback = Device().CreateBuffer(
        {.size = 8 * 8 * 4, .usage = rhi::BufferUsage::TransferDst, .memory = rhi::MemoryLocation::GpuToCpu});
    const core::Result<rhi::PipelineHandle> compute =
        Device().CreateComputePipeline({.shader = rhi::ToSpirv(kComputeTestCsSpirv), .name = "Compute test"});
    const core::Result<rhi::PipelineHandle> triangle =
        Device().CreatePipeline({.vertexShader = rhi::ToSpirv(kTriangleTestVsSpirv),
                                 .fragmentShader = rhi::ToSpirv(kTriangleTestPsSpirv),
                                 .colorFormat = rhi::Format::Rgba8Unorm,
                                 .cull = rhi::CullMode::None,
                                 .name = "Triangle"});
    ASSERT_TRUE(values && arguments && target && readback && compute && triangle);

    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        commands.BindPipeline(*compute);
                        commands.Push(
                            ComputeTestPush{Device().StorageIndex(*values), Device().StorageIndex(*arguments), 1});
                        commands.Dispatch(1);
                        commands.Barrier();
                        commands.Discard(*target, rhi::TextureState::ColorTarget);
                        commands.BeginRendering({.colorTarget = *target, .clearColor = glm::vec4(0.0f)});
                        commands.BindPipeline(*triangle);
                        commands.DrawIndirect(*arguments);
                        commands.EndRendering();
                        commands.Transition(*target, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(*target, *readback);
                    })
                    .has_value());

    const auto* pixels = reinterpret_cast<const std::uint8_t*>(Device().Mapped(*readback).data());
    EXPECT_EQ(pixels[(4 * 8 + 4) * 4], 255) << "the triangle covers the target";
    Device().DestroyPipeline(*compute);
    Device().DestroyPipeline(*triangle);
    Device().DestroyTexture(*target);
    for (const rhi::BufferHandle buffer : {*values, *arguments, *readback}) {
        Device().DestroyBuffer(buffer);
    }
}

TEST_F(GpuTest, MultisampledTargetsResolveToTheAverageColorAndTheFirstSamplesDepth) {
    constexpr rhi::Extent kExtent{8, 8};
    const std::uint32_t samples = std::min(4U, Device().Info().maxSamples);
    if (samples < 2) {
        GTEST_SKIP() << "The device has no multisampling";
    }
    const auto texture = [&](rhi::Format format, rhi::TextureUsage usage, std::uint32_t count, const char* name) {
        return Device()
            .CreateTexture({.extent = kExtent, .format = format, .usage = usage, .samples = count, .name = name})
            .value_or(rhi::TextureHandle{});
    };
    const rhi::TextureHandle color = texture(rhi::Format::Rgba8Unorm, rhi::TextureUsage::ColorTarget, samples, "Color");
    const rhi::TextureHandle depth = texture(rhi::Format::D32Float, rhi::TextureUsage::DepthTarget, samples, "Depth");
    const rhi::TextureHandle resolved = texture(
        rhi::Format::Rgba8Unorm, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc, 1, "Resolved");
    const rhi::TextureHandle resolvedDepth = texture(
        rhi::Format::D32Float, rhi::TextureUsage::DepthTarget | rhi::TextureUsage::TransferSrc, 1, "Resolved depth");
    const core::Result<rhi::BufferHandle> readback = Device().CreateBuffer(
        {.size = 8 * 8 * 4, .usage = rhi::BufferUsage::TransferDst, .memory = rhi::MemoryLocation::GpuToCpu});
    const core::Result<rhi::BufferHandle> depthReadback = Device().CreateBuffer(
        {.size = 8 * 8 * 4, .usage = rhi::BufferUsage::TransferDst, .memory = rhi::MemoryLocation::GpuToCpu});
    const core::Result<rhi::PipelineHandle> triangle =
        Device().CreatePipeline({.vertexShader = rhi::ToSpirv(kHalfTriangleTestVsSpirv),
                                 .fragmentShader = rhi::ToSpirv(kHalfTriangleTestPsSpirv),
                                 .colorFormat = rhi::Format::Rgba8Unorm,
                                 .depthFormat = rhi::Format::D32Float,
                                 .cull = rhi::CullMode::None,
                                 .samples = samples,
                                 .name = "Half triangle"});
    ASSERT_TRUE(color && depth && resolved && resolvedDepth && readback && depthReadback && triangle);

    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        for (const rhi::TextureHandle target : {color, resolved}) {
                            commands.Discard(target, rhi::TextureState::ColorTarget);
                        }
                        commands.Discard(depth, rhi::TextureState::DepthTarget);
                        const rhi::TextureUse depthResolve{resolvedDepth, rhi::Access::DepthResolve, true};
                        commands.Use({&depthResolve, 1}, {});
                        commands.BeginRendering({.colorTarget = color,
                                                 .clearColor = glm::vec4(0.0f),
                                                 .depthTarget = depth,
                                                 .clearDepth = 0.0f,
                                                 .colorResolve = resolved,
                                                 .depthResolve = resolvedDepth});
                        commands.BindPipeline(*triangle);
                        commands.Draw(3);
                        commands.EndRendering();
                        commands.Transition(resolved, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(resolved, *readback);
                        commands.Transition(resolvedDepth, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(resolvedDepth, *depthReadback);
                    })
                    .has_value());

    // Rows go down: below the diagonal, white; above, black; on it, half the samples of each.
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(Device().Mapped(*readback).data());
    const auto red = [&](int x, int y) { return pixels[(y * 8 + x) * 4]; };
    EXPECT_EQ(red(6, 1), 0);
    EXPECT_EQ(red(1, 6), 255);
    int between = 0;
    for (int i = 0; i < 8; ++i) {
        between += (red(i, i) > 0 && red(i, i) < 255) ? 1 : 0;
    }
    EXPECT_GT(between, 0) << "pixels on the edge average their samples";
    float depthInside = 0.0f;
    std::memcpy(&depthInside, Device().Mapped(*depthReadback).data() + (6 * 8 + 1) * 4, sizeof(float));
    EXPECT_EQ(depthInside, 1.0f);

    Device().DestroyPipeline(*triangle);
    for (const rhi::TextureHandle target : {color, depth, resolved, resolvedDepth}) {
        Device().DestroyTexture(target);
    }
    Device().DestroyBuffer(*readback);
    Device().DestroyBuffer(*depthReadback);
}
