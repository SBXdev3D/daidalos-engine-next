// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/CommandList.hpp>
#include <daidalos/rhi/Types.hpp>

#include <cstddef>
#include <cstdint>
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
    /// Instance extensions needed by the window system (platform::Window::VulkanInstanceExtensions).
    std::vector<std::string> instanceExtensions;
    /// Requires a device that can present to a Swapchain. Off for headless use.
    bool presentation = false;
    /// Anisotropic filtering of the linear samplers, from 1 (off) to 16, clamped to what the device allows.
    float anisotropy = 16.0f;
};

struct DeviceInfo {
    std::string name;
    bool validationEnabled = false;
    /// The anisotropy the linear samplers use: 1 when it is off or unsupported.
    float anisotropy = 1.0f;
};

/// One frame being recorded.
struct Frame {
    CommandList commands;
    /// Null without a swapchain. It starts in the Undefined or Present state and must be
    /// transitioned before use. EndFrame transitions it to Present.
    TextureHandle backBuffer;
    Extent extent;
};

/// Owns the Vulkan instance, device, GPU memory and every resource (ADR-0007). Only one Device can
/// exist at a time, and it is used from one thread.
///
/// Resources are destroyed through a deferred queue: Destroy* invalidates the handle at once, and the
/// GPU object goes away when the timeline semaphore shows that the GPU no longer uses it. Resources
/// still alive when the Device is destroyed are released, with a warning. Destroying a null or stale
/// handle does nothing.
class Device {
public:
    /// Private to the module; named here because command lists refer to it.
    struct Impl;

    /// Fails with NotSupported if there is no Vulkan loader or no device with Vulkan 1.3 and the
    /// required features, and with DeviceError if the device cannot be created.
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
    /// for buffers. The levels follow each other from the largest, tightly packed (MipByteSize).
    /// The texture needs TextureUsage::TransferDst and Sampled.
    [[nodiscard]] core::Result<void> UploadTexture(TextureHandle texture, std::span<const std::byte> pixels);
    /// Index of a sampled texture in the g_textures array of shaders.
    [[nodiscard]] std::uint32_t BindlessIndex(TextureHandle texture) const;
    /// Index of a BufferUsage::Storage buffer in the storage buffer arrays of shaders.
    [[nodiscard]] std::uint32_t StorageIndex(BufferHandle buffer) const;

    /// Fails with DeviceError if the driver rejects the SPIR-V.
    [[nodiscard]] core::Result<PipelineHandle> CreatePipeline(const GraphicsPipelineDesc& desc);
    void DestroyPipeline(PipelineHandle pipeline);

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
