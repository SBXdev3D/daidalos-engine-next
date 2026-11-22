// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/rhi/AccelerationStructure.hpp>
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
#include <filesystem>
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
inline constexpr std::uint32_t kMaxBindlessBuffers = 4096;
/// Cube textures that shaders can sample at the same time.
inline constexpr std::uint32_t kMaxBindlessCubes = 64;
/// Storage textures that shaders can read and write at the same time. A resize holds twice the targets for
/// a few frames: those in flight keep their slots until the GPU is done with them.
inline constexpr std::uint32_t kMaxStorageImages = 128;
/// Top-level acceleration structures that shaders can trace at the same time.
inline constexpr std::uint32_t kMaxBindlessAccelerationStructures = 16;
/// Structures built with the uploads whose compacted size is not read yet: more are left uncompacted.
inline constexpr std::uint32_t kMaxPendingCompactions = 4096;

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
    /// Tracked by CommandList::Use.
    AccessState access;
};

struct TextureData {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    /// Null for swapchain images, which the swapchain owns.
    VmaAllocation allocation = nullptr;
    Extent extent;
    std::uint32_t mipLevels = 1;
    /// 6 for a cube, 1 otherwise.
    std::uint32_t layers = 1;
    /// Slices of a 3D texture; 1 otherwise.
    std::uint32_t depth = 1;
    Format format = Format::Undefined;
    TextureUsage usage = TextureUsage::None;
    AccessState access;
    std::uint32_t bindlessIndex = 0;
    std::uint32_t storageIndex = 0;
};

struct AccelerationStructureData {
    VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
    VkBuffer storage = VK_NULL_HANDLE;
    VmaAllocation storageAllocation = nullptr;
    /// Released after a build with the uploads (Device::BuildWithUploads): such a structure is built once.
    VkBuffer scratch = VK_NULL_HANDLE;
    VmaAllocation scratchAllocation = nullptr;
    VkDeviceAddress scratchAddress = 0;
    VkDeviceAddress address = 0;
    AccelerationStructureDesc desc;
    std::uint64_t size = 0;
    /// Tracked by CommandList::Use; covers the scratch memory too.
    AccessState access;
    /// A build was recorded: a refit structure is updated from now on.
    bool built = false;
    /// Slot in the shaders' array, for top-level structures.
    std::uint32_t bindlessIndex = 0;
};

struct PipelineData {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

namespace detail {

struct CommandRecording {
    Device::Impl* device = nullptr;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    /// First query of this recording in the timestamp pool, for frames on a device with timestamps.
    std::optional<std::uint32_t> firstTimestamp;
    std::uint32_t timestampCount = 0;
    /// Recorded for the compute queue (ADR-0095): its barriers keep only the stages that queue has.
    bool computeOnly = false;
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
    /// Starts from the cache in the file, if this GPU and driver wrote it.
    core::Result<void> CreatePipelineCache(const std::filesystem::path& file);
    core::Result<void> SavePipelineCache() const;
    /// Counts a pipeline that took this long to create, and reports it if creation is locked.
    void NotePipeline(const std::string& name, double seconds);

    void SetName(VkObjectType type, std::uint64_t object, const std::string& name) const;
    /// Destroys a structure the GPU no longer uses.
    void DestroyAccelerationStructureNow(const AccelerationStructureData& data);

    /// Value that the timeline semaphore reaches once what is being recorded now has finished.
    [[nodiscard]] std::uint64_t NextTimelineValue() const { return submittedValue + 1; }
    [[nodiscard]] core::Result<std::uint64_t> CompletedValue() const;
    [[nodiscard]] core::Result<void> WaitForValue(std::uint64_t value) const;
    /// Runs destroy once the GPU can no longer use what was recorded so far.
    void Defer(std::function<void()> destroy);
    void CollectGarbage(std::uint64_t completed);
    /// Reads the timestamps of a finished frame into completedTimestamps.
    [[nodiscard]] core::Result<void> ReadTimestamps(detail::CommandRecording& recording, std::vector<double>& into);
    /// Submits recorded commands, which signal the next timeline value, and returns that value.
    [[nodiscard]] core::Result<std::uint64_t> SubmitCommands(VkCommandBuffer commands);
    /// The command list that collects the copies of UploadBuffer and UploadTexture.
    [[nodiscard]] core::Result<CommandList> UploadCommands();
    /// Submits the collected copies, if any, before anything else reaches the queue. Everything
    /// submitted later sees their results, without waiting for them.
    [[nodiscard]] core::Result<void> FlushUploads();
    /// Replaces each bottom-level structure built with the uploads, once its build is done, by a compacted copy:
    /// recorded with the uploads, the old one destroyed after it (ADR-0100). Outside frames.
    [[nodiscard]] core::Result<void> CompactBuiltStructures();

    struct Deferred {
        std::uint64_t retireValue = 0;
        std::function<void()> destroy;
    };

    struct FrameSlot {
        VkCommandPool pool = VK_NULL_HANDLE;
        detail::CommandRecording recording;
        std::uint64_t submittedValue = 0;
        /// The frame's graphics commands, in up to three parts (ADR-0095): before the compute queue starts,
        /// beside it, and after it. recording.commands is the part being recorded.
        std::array<VkCommandBuffer, 3> parts{};
        std::uint32_t part = 0;
        /// The compute queue's commands of the frame, and whether this frame records any.
        VkCommandPool computePool = VK_NULL_HANDLE;
        detail::CommandRecording computeRecording;
        bool forked = false;
    };

    /// Sets a resource's sharing between the graphics and compute queues: concurrent when both exist.
    template <typename CreateInfo>
    void Share(CreateInfo& create) const {
        if (computeQueue != VK_NULL_HANDLE) {
            create.sharingMode = VK_SHARING_MODE_CONCURRENT;
            create.queueFamilyIndexCount = static_cast<std::uint32_t>(sharedFamilies.size());
            create.pQueueFamilyIndices = sharedFamilies.data();
        }
    }

    DeviceInfo info;
    std::atomic<std::uint64_t> validationErrors{0};

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool debugUtils = false;
    /// Swapchains may use color spaces other than sRGB, such as HDR10 (VK_EXT_swapchain_colorspace).
    bool colorSpaces = false;
    /// Swapchains can describe their HDR content to the display (VK_EXT_hdr_metadata).
    bool hdrMetadata = false;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    /// A queue of a family that computes but does not draw, for passes that run beside the frame's drawing
    /// (ADR-0095). Null when the device has none or DeviceConfig::asyncCompute is off.
    std::uint32_t computeFamily = 0;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::array<std::uint32_t, 2> sharedFamilies{};
    /// Orders the compute queue's part of a frame after the graphics commands before it, and the graphics
    /// commands after it after it: each fork signals one value, each end of the compute part the next.
    VkSemaphore asyncTimeline = VK_NULL_HANDLE;
    std::uint64_t asyncValue = 0;
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
    std::vector<double> completedComputeTimestamps;
    /// Copies waiting for FlushUploads, recorded in a buffer of the immediate pool.
    detail::CommandRecording uploads;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet globalSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::filesystem::path pipelineCacheFile;
    bool pipelinesLocked = false;
    /// DeviceConfig::pipelineStatistics, on a device that has the extension.
    bool pipelineStatistics = false;
    /// Logs the driver's statistics of a pipeline created with VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR.
    void LogPipelineStatistics(VkPipeline pipeline, const std::string& name) const;
    std::uint64_t latePipelines = 0;
    double pipelineSeconds = 0.0;
    std::array<VkSampler, kSamplerCount> samplers{};
    std::vector<std::uint32_t> freeBindlessIndices;
    std::vector<std::uint32_t> freeStorageIndices;
    std::vector<std::uint32_t> freeAccelerationStructureIndices;
    std::vector<std::uint32_t> freeStorageImageIndices;
    std::vector<std::uint32_t> freeCubeIndices;
    VkDeviceSize scratchAlignment = 256;

    core::SlotPool<BufferData, BufferTag> buffers;
    core::SlotPool<TextureData, TextureTag> textures;
    core::SlotPool<PipelineData, PipelineTag> pipelines;
    core::SlotPool<AccelerationStructureData, AccelerationStructureTag> accelerationStructures;
    std::deque<Deferred> deferred;
    /// The compacted sizes of the structures built with the uploads, one query each (ADR-0100).
    VkQueryPool compactionQueries = VK_NULL_HANDLE;
    std::vector<std::uint32_t> freeCompactionQueries;
    struct PendingCompaction {
        AccelerationStructureHandle structure;
        std::uint32_t query = 0;
        /// The timeline value of the uploads that hold the build and the query; 0 until they are submitted.
        std::uint64_t submitted = 0;
    };
    std::vector<PendingCompaction> pendingCompactions;

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
    /// Whether the last color space logged was HDR10.
    std::optional<bool> loggedHdr;
    SwapchainConfig config;
    Extent extent;
    bool outdated = true;

    std::vector<SwapchainImage> images;
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::uint32_t acquiredImage = 0;
    VkSemaphore acquiredSemaphore = VK_NULL_HANDLE;
};

} // namespace daidalos::rhi
