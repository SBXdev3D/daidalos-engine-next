// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/rhi/Device.hpp>
#include <daidalos/rhi/Swapchain.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

std::optional<VkSurfaceFormatKHR> ChooseFormat(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());

    // The swapchain format converts linear lighting to sRGB (CONVENTIONS.md, section 6).
    for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
    }
    return std::nullopt;
}

VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice device, VkSurfaceKHR surface, bool vsync) {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR; // Always available.
    }
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes.data());
    for (const VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
        if (std::ranges::find(modes, wanted) != modes.end()) {
            return wanted;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

Swapchain::Impl::~Impl() {
    if (device == nullptr) {
        return;
    }
    vkDeviceWaitIdle(device->device);
    DestroyImages();
    for (VkSemaphore semaphore : imageAvailable) {
        vkDestroySemaphore(device->device, semaphore, nullptr);
    }
    vkDestroySwapchainKHR(device->device, swapchain, nullptr);
    vkDestroySurfaceKHR(device->instance, surface, nullptr);
    --device->swapchains;
}

void Swapchain::Impl::DestroyImages() {
    for (SwapchainImage& image : images) {
        if (std::optional<TextureData> texture = device->textures.Remove(image.texture)) {
            vkDestroyImageView(device->device, texture->view, nullptr);
        }
        vkDestroySemaphore(device->device, image.renderFinished, nullptr);
    }
    images.clear();
}

core::Result<void> Swapchain::Impl::Recreate() {
    if (const VkResult result = vkQueueWaitIdle(device->queue); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkQueueWaitIdle", result));
    }
    DestroyImages();

    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physicalDevice, surface, &capabilities);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result));
    }

    // A current extent of UINT32_MAX means that the surface takes the size of the swapchain.
    VkExtent2D chosen = capabilities.currentExtent;
    if (chosen.width == std::numeric_limits<std::uint32_t>::max()) {
        chosen.width =
            std::clamp(config.extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        chosen.height =
            std::clamp(config.extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    extent = {chosen.width, chosen.height};
    if (extent.width == 0 || extent.height == 0) {
        return {}; // Minimized. outdated stays set, so the next BeginFrame tries again.
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const VkPresentModeKHR presentMode = ChoosePresentMode(device->physicalDevice, surface, config.vsync);
    const VkSwapchainKHR previous = swapchain;
    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = chosen,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = previous,
    };
    const VkResult created = vkCreateSwapchainKHR(device->device, &createInfo, nullptr, &swapchain);
    vkDestroySwapchainKHR(device->device, previous, nullptr);
    if (created != VK_SUCCESS) {
        swapchain = VK_NULL_HANDLE;
        return tl::unexpected(VulkanError("vkCreateSwapchainKHR", created));
    }

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device->device, swapchain, &count, nullptr);
    std::vector<VkImage> vulkanImages(count);
    vkGetSwapchainImagesKHR(device->device, swapchain, &count, vulkanImages.data());

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (const VkImage vulkanImage : vulkanImages) {
        SwapchainImage image{.image = vulkanImage};
        TextureData texture;
        texture.image = vulkanImage;
        texture.extent = extent;
        texture.format = FromVulkan(surfaceFormat.format);
        texture.usage = TextureUsage::ColorTarget | TextureUsage::TransferSrc;

        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vulkanImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        if (const VkResult result = vkCreateImageView(device->device, &viewInfo, nullptr, &texture.view);
            result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkCreateImageView", result));
        }
        if (const VkResult result = vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &image.renderFinished);
            result != VK_SUCCESS) {
            vkDestroyImageView(device->device, texture.view, nullptr);
            return tl::unexpected(VulkanError("vkCreateSemaphore", result));
        }
        image.texture = device->textures.Insert(texture);
        images.push_back(image);
    }

    // A failed present or acquire can leave these signaled, so they start afresh.
    for (VkSemaphore& semaphore : imageAvailable) {
        vkDestroySemaphore(device->device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore);
            result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkCreateSemaphore", result));
        }
    }

    outdated = false;
    DAI_LOG_DEBUG(kModule, "Swapchain created: {}x{}, {} images, present mode {}", extent.width, extent.height,
                  images.size(), static_cast<int>(presentMode));
    return {};
}

core::Result<TextureHandle> Swapchain::Impl::Acquire(std::uint32_t frameIndex) {
    if (outdated) {
        if (config.extent.width == 0 || config.extent.height == 0) {
            return TextureHandle{};
        }
        DAI_TRY(Recreate());
        if (outdated) {
            return TextureHandle{};
        }
    }

    const VkSemaphore semaphore = imageAvailable[frameIndex];
    const VkResult result = vkAcquireNextImageKHR(device->device, swapchain, std::numeric_limits<std::uint64_t>::max(),
                                                  semaphore, VK_NULL_HANDLE, &acquiredImage);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        outdated = true;
        return TextureHandle{};
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        outdated = true; // Still presentable: use the image, recreate on the next frame.
    } else if (result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkAcquireNextImageKHR", result));
    }
    acquiredSemaphore = semaphore;
    return images[acquiredImage].texture;
}

core::Result<void> Swapchain::Impl::Present() {
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &images[acquiredImage].renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &acquiredImage,
    };
    const VkResult result = vkQueuePresentKHR(device->queue, &presentInfo);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        ++device->presentedFrames;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        outdated = true;
        return {};
    }
    if (result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkQueuePresentKHR", result));
    }
    return {};
}

core::Result<Swapchain> Swapchain::Create(Device& device, const SurfaceFactory& createSurface,
                                          const SwapchainConfig& config) {
    Device::Impl& deviceImpl = *device.m_impl;

    DAI_TRY_ASSIGN(std::uint64_t surface, createSurface(reinterpret_cast<std::uintptr_t>(deviceImpl.instance)));

    auto impl = std::make_unique<Impl>();
    impl->device = &deviceImpl;
    ++deviceImpl.swapchains;
    impl->surface = reinterpret_cast<VkSurfaceKHR>(surface);
    impl->config = config;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(deviceImpl.physicalDevice, deviceImpl.queueFamily, impl->surface, &supported);
    if (supported == VK_FALSE) {
        return core::MakeError(core::ErrorCode::NotSupported, kModule,
                               std::format("'{}' cannot present to this window", deviceImpl.info.name));
    }
    const std::optional<VkSurfaceFormatKHR> format = ChooseFormat(deviceImpl.physicalDevice, impl->surface);
    if (!format.has_value()) {
        return core::MakeError(core::ErrorCode::NotSupported, kModule, "The window has no 8-bit sRGB surface format");
    }
    impl->surfaceFormat = *format;

    if (config.extent.width != 0 && config.extent.height != 0) {
        DAI_TRY(impl->Recreate());
    }
    return Swapchain(std::move(impl));
}

Swapchain::Swapchain(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Swapchain::Swapchain(Swapchain&& other) noexcept = default;
Swapchain& Swapchain::operator=(Swapchain&& other) noexcept = default;
Swapchain::~Swapchain() = default;

void Swapchain::Configure(const SwapchainConfig& config) {
    if (config.extent != m_impl->config.extent || config.vsync != m_impl->config.vsync) {
        m_impl->config = config;
        m_impl->outdated = true;
    }
}

Format Swapchain::ColorFormat() const {
    return FromVulkan(m_impl->surfaceFormat.format);
}

Extent Swapchain::CurrentExtent() const {
    return m_impl->extent;
}

} // namespace daidalos::rhi
