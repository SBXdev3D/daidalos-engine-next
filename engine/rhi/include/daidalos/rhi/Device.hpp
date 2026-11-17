// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/AccelerationStructure.hpp>
#include <daidalos/rhi/CommandList.hpp>
#include <daidalos/rhi/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::rhi {

class Swapchain;

struct DeviceConfig {
    std::string applicationName = "Daidalos Engine Next";
    /// Enables the validation layers if they are installed. Without them a warning is logged.
    bool validation = false;
    /// With validation, also checks synchronization: hazards between uses that no barrier orders.
    /// Slow; the GPU tests turn it on.
    bool synchronizationValidation = false;
    /// Instance extensions needed by the window system (platform::Window::VulkanInstanceExtensions).
    std::vector<std::string> instanceExtensions;
    /// Requires a device that can present to a Swapchain. Off for headless use.
    bool presentation = false;
    /// Anisotropic filtering of the linear samplers, from 1 (off) to 16, clamped to what the device allows.
    float anisotropy = 16.0f;
    /// Where the pipeline cache is kept between runs (ADR-0052): read at creation, unless another GPU or
    /// driver wrote it, and written when the device is destroyed. Empty for none.
    std::filesystem::path pipelineCacheFile;
};

struct DeviceInfo {
    std::string name;
    bool validationEnabled = false;
    /// The anisotropy the linear samplers use: 1 when it is off or unsupported.
    float anisotropy = 1.0f;
    /// Whether the driver reports the memory in use and the budget (VK_EXT_memory_budget); without it,
    /// MemoryStats counts the engine's own allocations against most of the video memory.
    bool memoryBudget = false;
    /// Whether DeviceConfig::pipelineCacheFile held a cache this device could use.
    bool pipelineCacheLoaded = false;
};

/// One frame being recorded.
struct Frame {
    CommandList commands;
    /// Null without a swapchain. It starts in the Undefined or Present state and must be
    /// transitioned before use. EndFrame transitions it to Present.
    TextureHandle backBuffer;
    Extent extent;
    /// The frame slot being recorded, below kFramesInFlight. Device::CompletedTimestamps, right after
    /// the BeginFrame that returned this frame, holds the timestamps of the previous frame in this slot.
    std::uint32_t slot = 0;
};

/// Owns the Vulkan instance, device, GPU memory and every resource (ADR-0007). Only one Device can
/// exist at a time, and it is used from one thread.
///
/// Resources are destroyed through a deferred queue: Destroy* invalidates the handle at once, and the
/// GPU object goes away when the timeline semaphore shows that the GPU no longer uses it. Resources
/// still alive when the Device is destroyed are released, with a warning. Destroying a null or stale
/// handle does nothing.
/// What a device holds, for diagnostics.
struct MemoryStats {
    /// Bytes of device-local memory in use by this process, and how much the system lets it use.
    std::uint64_t usedBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::size_t buffers = 0;
    std::size_t textures = 0;
    std::size_t pipelines = 0;
    std::size_t accelerationStructures = 0;
    /// Bytes held by acceleration structures, scratch memory excluded.
    std::uint64_t accelerationStructureBytes = 0;
};

class Device {
public:
    /// Private to the module; named here because command lists refer to it.
    struct Impl;

    /// Fails with NotSupported if there is no Vulkan loader or no device with Vulkan 1.4, hardware ray
    /// tracing (acceleration structures and ray queries, ADR-0028) and the other required features; the
    /// message names what each device lacks. Fails with DeviceError if the device cannot be created.
    static core::Result<Device> Create(const DeviceConfig& config);

    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    [[nodiscard]] const DeviceInfo& Info() const;
    /// Errors reported by the validation layers so far. Stays 0 when they are not enabled.
    [[nodiscard]] std::uint64_t ValidationErrorCount() const;
    /// Swapchain images presented since the device was created.
    [[nodiscard]] std::uint64_t PresentedFrameCount() const;
    /// The timestamps of the last frame the GPU finished, from BeginFrame, in milliseconds after the
    /// first one, in the order they were written. Empty without timestamp support. Frames finish
    /// kFramesInFlight frames after they were recorded.
    [[nodiscard]] std::span<const double> CompletedTimestamps() const;
    /// Objects waiting for the GPU to finish with them.
    [[nodiscard]] std::size_t PendingDestructionCount() const;
    /// Device-local memory and live objects, now.
    [[nodiscard]] MemoryStats Memory() const;

    [[nodiscard]] core::Result<BufferHandle> CreateBuffer(const BufferDesc& desc);
    void DestroyBuffer(BufferHandle buffer);
    /// CPU view of a CpuToGpu or GpuToCpu buffer. Coherent: no flush is needed.
    [[nodiscard]] std::span<std::byte> Mapped(BufferHandle buffer);
    /// Copies data into a buffer through a staging buffer. The copy is only recorded: all the
    /// uploads since the last submission go to the GPU together, ahead of the next BeginFrame,
    /// Submit or WaitIdle, which see their results. Nothing waits for them. Not allowed between
    /// BeginFrame and EndFrame. The buffer needs BufferUsage::TransferDst.
    [[nodiscard]] core::Result<void> UploadBuffer(BufferHandle buffer, std::span<const std::byte> data);

    [[nodiscard]] core::Result<TextureHandle> CreateTexture(const TextureDesc& desc);
    void DestroyTexture(TextureHandle texture);
    /// Fills every mip level of a texture and leaves it in the ShaderRead state, as UploadBuffer does
    /// for buffers. The levels follow each other from the largest, tightly packed (MipByteSize); a cube's
    /// faces follow each other, each with its levels.
    /// The texture needs TextureUsage::TransferDst and Sampled.
    [[nodiscard]] core::Result<void> UploadTexture(TextureHandle texture, std::span<const std::byte> pixels);
    /// Index of a TextureUsage::Storage texture in the storage image arrays of shaders, at binding 4: one
    /// array per format, all at the same binding, each declaring its format.
    [[nodiscard]] std::uint32_t StorageImageIndex(TextureHandle texture) const;
    /// Index of a sampled texture in the g_textures array of shaders, or of a cube in g_cubeTextures.
    [[nodiscard]] std::uint32_t BindlessIndex(TextureHandle texture) const;
    /// The size of a texture's largest level.
    [[nodiscard]] Extent TextureExtent(TextureHandle texture) const;
    /// Index of a BufferUsage::Storage buffer in the storage buffer arrays of shaders.
    [[nodiscard]] std::uint32_t StorageIndex(BufferHandle buffer) const;

    /// Sizes and allocates an acceleration structure for its geometry or instance count. It holds nothing
    /// until it is built (CommandList::BuildAccelerationStructure, or BuildWithUploads).
    [[nodiscard]] core::Result<AccelerationStructureHandle>
    CreateAccelerationStructure(const AccelerationStructureDesc& desc);
    void DestroyAccelerationStructure(AccelerationStructureHandle structure);
    /// Index of a top-level structure in the g_accelerationStructures array of shaders.
    [[nodiscard]] std::uint32_t AccelerationStructureIndex(AccelerationStructureHandle structure) const;
    /// Bytes the structure holds.
    [[nodiscard]] std::uint64_t AccelerationStructureSize(AccelerationStructureHandle structure) const;
    /// Builds a bottom-level structure with the uploads, after the copies recorded so far, so that it
    /// can be built from freshly uploaded vertices. Its scratch memory is released afterwards: it cannot
    /// be built again. Not allowed between BeginFrame and EndFrame.
    [[nodiscard]] core::Result<void> BuildWithUploads(AccelerationStructureHandle structure);
    /// Writes instances for a top-level build into a CpuToGpu buffer with
    /// BufferUsage::AccelerationStructureInput, kAccelerationStructureInstanceSize bytes each, from offset.
    void WriteInstances(BufferHandle buffer, std::uint64_t offset,
                        std::span<const AccelerationStructureInstance> instances);

    /// Fails with DeviceError if the driver rejects the SPIR-V.
    [[nodiscard]] core::Result<PipelineHandle> CreatePipeline(const GraphicsPipelineDesc& desc);
    /// Fails with DeviceError if the driver rejects the SPIR-V.
    [[nodiscard]] core::Result<PipelineHandle> CreateComputePipeline(const ComputePipelineDesc& desc);
    void DestroyPipeline(PipelineHandle pipeline);
    /// Once a level has loaded, no pipeline should be created: creating one stalls the frame (ADR-0052).
    /// While locked, a pipeline is still created, but logged as an error and counted.
    void LockPipelineCreation(bool locked);
    [[nodiscard]] bool PipelineCreationLocked() const;
    /// Pipelines created while creation was locked.
    [[nodiscard]] std::uint64_t LatePipelineCount() const;
    /// Seconds spent creating pipelines since the device was created.
    [[nodiscard]] double PipelineCreationSeconds() const;
    /// Writes the pipeline cache to DeviceConfig::pipelineCacheFile, which destroying the device also does.
    [[nodiscard]] core::Result<void> SavePipelineCache();

    /// Waits for a free frame slot, acquires the next swapchain image if there is a swapchain, and
    /// starts recording. Returns nothing when this frame cannot be drawn (zero-sized window, swapchain
    /// recreated): skip it and do not call EndFrame.
    [[nodiscard]] core::Result<std::optional<Frame>> BeginFrame(Swapchain* swapchain = nullptr);
    /// Submits the frame and presents the swapchain image. Takes the swapchain given to BeginFrame.
    [[nodiscard]] core::Result<void> EndFrame(Swapchain* swapchain = nullptr);

    /// Records and submits commands outside the frame loop, and waits for them.
    /// Not allowed between BeginFrame and EndFrame.
    [[nodiscard]] core::Result<void> Submit(const std::function<void(CommandList&)>& record);

    /// Waits for the GPU to finish, then destroys everything that was queued for destruction.
    [[nodiscard]] core::Result<void> WaitIdle();

private:
    friend class Swapchain;

    explicit Device(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::rhi
