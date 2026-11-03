// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/rhi/Device.hpp>
#include <daidalos/rhi/Swapchain.hpp>
#include <daidalos/rhi/Types.hpp>

// clang-format off: volk must come before anything that includes Vulkan headers.
#include <volk.h>
#include <vk_mem_alloc.h>
// clang-format on

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::rhi {

inline constexpr std::string_view kModule = "rhi";
/// Size of the g_textures array of shaders.
inline constexpr std::uint32_t kMaxBindlessTextures = 4096;
/// Size of the storage buffer arrays of shaders.
inline constexpr std::uint32_t kMaxBindlessBuffers = 1024;

[[nodiscard]] core::Error VulkanError(std::string_view operation, VkResult result);
[[nodiscard]] VkFormat ToVulkan(Format format);
[[nodiscard]] Format FromVulkan(VkFormat format);

struct BufferData {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    std::span<std::byte> mapped;
    BufferUsage usage = BufferUsage::None;
    std::uint64_t size = 0;
    std::uint32_t storageIndex = 0;
};

struct TextureData {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    /// Null for swapchain images, which the swapchain owns.
    VmaAllocation allocation = nullptr;
    Extent extent;
    std::uint32_t mipLevels = 1;
    Format format = Format::Undefined;
    TextureUsage usage = TextureUsage::None;
    TextureState state = TextureState::Undefined;
    std::uint32_t bindlessIndex = 0;
};

struct PipelineData {
    VkPipeline pipeline = VK_NULL_HANDLE;
};

namespace detail {

struct CommandRecording {
    Device::Impl* device = nullptr;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    /// First query of this recording in the timestamp pool, for frames on a device with timestamps.
    std::optional<std::uint32_t> firstTimestamp;
    std::uint32_t timestampCount = 0;
};

} // namespace detail

struct Device::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    /// Also releases a partially created device, so that Device::Create can return at any step.
    ~Impl();

    core::Result<void> CreateInstance(const DeviceConfig& config);
    core::Result<void> SelectPhysicalDevice(const DeviceConfig& config);
    core::Result<void> CreateLogicalDevice(const DeviceConfig& config);
    core::Result<void> CreateAllocator();
    core::Result<void> CreateFrameResources();
    core::Result<void> CreateGlobalDescriptors();

    void SetName(VkObjectType type, std::uint64_t object, const std::string& name) const;

    /// Value that the timeline semaphore reaches once what is being recorded now has finished.
    [[nodiscard]] std::uint64_t NextTimelineValue() const { return submittedValue + 1; }
    [[nodiscard]] core::Result<std::uint64_t> CompletedValue() const;
    [[nodiscard]] core::Result<void> WaitForValue(std::uint64_t value) const;
    /// Runs destroy once the GPU can no longer use what was recorded so far.
    void Defer(std::function<void()> destroy);
    void CollectGarbage(std::uint64_t completed);
    /// Reads the timestamps of a finished frame into completedTimestamps.
    [[nodiscard]] core::Result<void> ReadTimestamps(detail::CommandRecording& recording);
    /// Submits recorded commands, which signal the next timeline value, and returns that value.
    [[nodiscard]] core::Result<std::uint64_t> SubmitCommands(VkCommandBuffer commands);
    /// The command list that collects the copies of UploadBuffer and UploadTexture.
    [[nodiscard]] core::Result<CommandList> UploadCommands();
    /// Submits the collected copies, if any, before anything else reaches the queue. Everything
    /// submitted later sees their results, without waiting for them.
    [[nodiscard]] core::Result<void> FlushUploads();

    struct Deferred {
        std::uint64_t retireValue = 0;
        std::function<void()> destroy;
    };

    struct FrameSlot {
        VkCommandPool pool = VK_NULL_HANDLE;
        detail::CommandRecording recording;
        std::uint64_t submittedValue = 0;
    };

    DeviceInfo info;
    std::atomic<std::uint64_t> validationErrors{0};

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool debugUtils = false;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;

    // One semaphore orders everything: each submission signals the next value (ADR-0007).
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::uint64_t submittedValue = 0;

    std::array<FrameSlot, kFramesInFlight> frames;
    std::uint32_t frameIndex = 0;
    bool frameOpen = false;
    std::uint64_t presentedFrames = 0;
    VkCommandPool immediatePool = VK_NULL_HANDLE;
    /// kMaxTimestamps queries per frame slot. Null when the queue cannot write timestamps.
    VkQueryPool timestamps = VK_NULL_HANDLE;
    double timestampPeriodMs = 0.0;
    std::vector<double> completedTimestamps;
    /// Copies waiting for FlushUploads, recorded in a buffer of the immediate pool.
    detail::CommandRecording uploads;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet globalSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::array<VkSampler, 3> samplers{};
    std::vector<std::uint32_t> freeBindlessIndices;
    std::vector<std::uint32_t> freeStorageIndices;

    core::SlotPool<BufferData, BufferTag> buffers;
    core::SlotPool<TextureData, TextureTag> textures;
    core::SlotPool<PipelineData, PipelineTag> pipelines;
    std::deque<Deferred> deferred;

    /// Live swapchains; each must be destroyed before the device.
    std::uint32_t swapchains = 0;
};

/// One per swapchain image, because a semaphore waited on by a present cannot be reused until that
/// image comes back.
struct SwapchainImage {
    VkImage image = VK_NULL_HANDLE;
    TextureHandle texture;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
};

struct Swapchain::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl();

    core::Result<void> Recreate();
    void DestroyImages();
    /// Null handle in the result means that this frame cannot be drawn.
    core::Result<TextureHandle> Acquire(std::uint32_t frameIndex);
    core::Result<void> Present();

    Device::Impl* device = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat{};
    SwapchainConfig config;
    Extent extent;
    bool outdated = true;

    std::vector<SwapchainImage> images;
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::uint32_t acquiredImage = 0;
    VkSemaphore acquiredSemaphore = VK_NULL_HANDLE;
};

} // namespace daidalos::rhi
