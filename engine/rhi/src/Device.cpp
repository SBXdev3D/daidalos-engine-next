// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/rhi/Device.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

// Vulkan has process-wide function pointers (volk), so only one Device may exist at a time.
std::atomic<bool> s_deviceExists{false};

VKAPI_ATTR VkBool32 VKAPI_CALL OnValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        DAI_LOG_ERROR(kModule, "Validation: {}", data->pMessage);
        static_cast<std::atomic<std::uint64_t>*>(userData)->fetch_add(1);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        DAI_LOG_WARNING(kModule, "Validation: {}", data->pMessage);
    }
    return VK_FALSE;
}

bool HasLayer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::ranges::any_of(
        layers, [name](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool HasInstanceExtension(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

// Returns the reason why the device cannot be used, or an empty string.
std::string MissingRequirement(VkPhysicalDevice device, const DeviceConfig& config) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return "Vulkan 1.3";
    }

    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                .pNext = &features13};
    VkPhysicalDeviceFeatures2 features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12};
    vkGetPhysicalDeviceFeatures2(device, &features);

    if (features13.dynamicRendering == VK_FALSE) {
        return "dynamic rendering";
    }
    if (features13.synchronization2 == VK_FALSE) {
        return "synchronization2";
    }
    if (features12.timelineSemaphore == VK_FALSE) {
        return "timeline semaphores";
    }
    if (features12.runtimeDescriptorArray == VK_FALSE || features12.descriptorBindingPartiallyBound == VK_FALSE ||
        features12.descriptorBindingSampledImageUpdateAfterBind == VK_FALSE ||
        features12.shaderSampledImageArrayNonUniformIndexing == VK_FALSE) {
        return "descriptor indexing";
    }
    if (config.presentation && !HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return "the swapchain extension";
    }
    return {};
}

std::optional<std::uint32_t> FindGraphicsQueue(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (std::uint32_t index = 0; index < count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return index;
        }
    }
    return std::nullopt;
}

int Score(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 0; // lavapipe, picked only when nothing else exists.
    default:
        return 1;
    }
}

} // namespace

core::Error VulkanError(std::string_view operation, VkResult result) {
    const core::ErrorCode code = (result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY)
                                     ? core::ErrorCode::OutOfMemory
                                     : core::ErrorCode::DeviceError;
    return core::Error{code, std::string(kModule),
                       std::format("{} failed with VkResult {}", operation, static_cast<int>(result))};
}

Device::Impl::~Impl() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        CollectGarbage(std::numeric_limits<std::uint64_t>::max());

        const std::size_t leaked = buffers.Size() + textures.Size() + pipelines.Size();
        if (leaked != 0) {
            DAI_LOG_WARNING(kModule, "{} GPU resources were still alive when the device was destroyed", leaked);
        }
        buffers.ForEach([this](BufferData& data) { vmaDestroyBuffer(allocator, data.buffer, data.allocation); });
        textures.ForEach([this](TextureData& data) {
            vkDestroyImageView(device, data.view, nullptr);
            vmaDestroyImage(allocator, data.image, data.allocation);
        });
        pipelines.ForEach([this](PipelineData& data) { vkDestroyPipeline(device, data.pipeline, nullptr); });

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        for (VkSampler sampler : samplers) {
            vkDestroySampler(device, sampler, nullptr);
        }
        for (FrameSlot& frame : frames) {
            vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        vkDestroyCommandPool(device, immediatePool, nullptr);
        vkDestroySemaphore(device, timeline, nullptr);
        if (allocator != nullptr) {
            vmaDestroyAllocator(allocator);
        }
        vkDestroyDevice(device, nullptr);
    }
    if (messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
    volkFinalize();
    s_deviceExists.store(false);
}

core::Result<void> Device::Impl::CreateInstance(const DeviceConfig& config) {
    if (const VkResult result = volkInitialize(); result != VK_SUCCESS) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                               "No Vulkan loader found: install a graphics driver with Vulkan support");
    }

    std::uint32_t version = 0;
    if (vkEnumerateInstanceVersion == nullptr || vkEnumerateInstanceVersion(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_3) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                               "The Vulkan loader does not support Vulkan 1.3");
    }

    std::vector<const char*> extensions;
    extensions.reserve(config.instanceExtensions.size() + 1);
    for (const std::string& extension : config.instanceExtensions) {
        extensions.push_back(extension.c_str());
    }
    std::vector<const char*> layers;
    if (config.validation) {
        if (HasLayer(kValidationLayer)) {
            layers.push_back(kValidationLayer);
        } else {
            DAI_LOG_WARNING(kModule, "Validation was requested but {} is not installed", kValidationLayer);
        }
    }
    // Object names are worth having whenever a debugger or the validation layers can show them.
    debugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
        .pfnUserCallback = OnValidationMessage,
        .pUserData = &validationErrors,
    };
    const bool useMessenger = !layers.empty() && debugUtils;

    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = config.applicationName.c_str(),
        .pEngineName = "Daidalos Engine Next",
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = useMessenger ? &messengerInfo : nullptr,
        .pApplicationInfo = &application,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance); result != VK_SUCCESS) {
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                                   "No Vulkan 1.3 driver is available");
        }
        return tl::unexpected(VulkanError("vkCreateInstance", result));
    }
    volkLoadInstanceOnly(instance);

    if (useMessenger) {
        if (const VkResult result = vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, nullptr, &messenger);
            result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkCreateDebugUtilsMessengerEXT", result));
        }
    }
    info.validationEnabled = useMessenger;
    return {};
}

core::Result<void> Device::Impl::SelectPhysicalDevice(const DeviceConfig& config) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (devices.empty()) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule), "No Vulkan device found");
    }

    std::string rejected;
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        std::string missing = MissingRequirement(candidate, config);
        const std::optional<std::uint32_t> family = FindGraphicsQueue(candidate);
        if (missing.empty() && !family.has_value()) {
            missing = "a graphics queue";
        }
        if (!missing.empty()) {
            rejected += std::format(" '{}' lacks {}.", properties.deviceName, missing);
            continue;
        }
        if (const int score = Score(candidate); score > bestScore) {
            bestScore = score;
            physicalDevice = candidate;
            queueFamily = *family;
            info.name = properties.deviceName;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                               std::format("No suitable Vulkan device:{}", rejected));
    }
    return {};
}

core::Result<void> Device::Impl::CreateLogicalDevice(const DeviceConfig& config) {
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .timelineSemaphore = VK_TRUE,
    };

    std::vector<const char*> extensions;
    if (config.presentation) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features12,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateDevice", result));
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    return {};
}

core::Result<void> Device::Impl::CreateAllocator() {
    const VmaVulkanFunctions functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo createInfo{
        .physicalDevice = physicalDevice,
        .device = device,
        .pVulkanFunctions = &functions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    if (const VkResult result = vmaCreateAllocator(&createInfo, &allocator); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vmaCreateAllocator", result));
    }
    return {};
}

core::Result<void> Device::Impl::CreateFrameResources() {
    const VkSemaphoreTypeCreateInfo timelineType{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineType};
    if (const VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timeline); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateSemaphore", result));
    }

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queueFamily,
    };
    if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &immediatePool); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateCommandPool", result));
    }
    for (FrameSlot& frame : frames) {
        if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool);
            result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkCreateCommandPool", result));
        }
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        frame.recording.device = this;
        if (const VkResult result = vkAllocateCommandBuffers(device, &allocateInfo, &frame.recording.commands);
            result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkAllocateCommandBuffers", result));
        }
    }
    return {};
}

core::Result<void> Device::Impl::CreateGlobalDescriptors() {
    const auto createSampler = [this](VkFilter filter, VkSamplerAddressMode address, VkSampler& sampler) {
        const VkSamplerCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = filter,
            .minFilter = filter,
            .mipmapMode = (filter == VK_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = address,
            .addressModeV = address,
            .addressModeW = address,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        return vkCreateSampler(device, &createInfo, nullptr, &sampler);
    };
    if (const VkResult result = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              samplers[static_cast<std::size_t>(SamplerIndex::LinearClamp)]);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateSampler", result));
    }
    if (const VkResult result = createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                              samplers[static_cast<std::size_t>(SamplerIndex::LinearRepeat)]);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateSampler", result));
    }
    if (const VkResult result = createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              samplers[static_cast<std::size_t>(SamplerIndex::NearestClamp)]);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateSampler", result));
    }

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = kMaxBindlessTextures,
         .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
         .descriptorCount = static_cast<std::uint32_t>(samplers.size()),
         .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
         .pImmutableSamplers = samplers.data()},
    }};
    const std::array<VkDescriptorBindingFlags, 2> bindingFlags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    if (const VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateDescriptorSetLayout", result));
    }

    const std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxBindlessTextures},
        {VK_DESCRIPTOR_TYPE_SAMPLER, static_cast<std::uint32_t>(samplers.size())},
    }};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    if (const VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateDescriptorPool", result));
    }

    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &setLayout,
    };
    if (const VkResult result = vkAllocateDescriptorSets(device, &allocateInfo, &globalSet); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkAllocateDescriptorSets", result));
    }

    const VkPushConstantRange pushConstants{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset = 0,
        .size = kMaxPushConstantSize,
    };
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstants,
    };
    if (const VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreatePipelineLayout", result));
    }

    freeBindlessIndices.reserve(kMaxBindlessTextures);
    for (std::uint32_t index = kMaxBindlessTextures; index > 0; --index) {
        freeBindlessIndices.push_back(index - 1);
    }
    return {};
}

void Device::Impl::SetName(VkObjectType type, std::uint64_t object, const std::string& name) const {
    if (!debugUtils || name.empty()) {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = object,
        .pObjectName = name.c_str(),
    };
    vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
}

core::Result<std::uint64_t> Device::Impl::CompletedValue() const {
    std::uint64_t value = 0;
    if (const VkResult result = vkGetSemaphoreCounterValue(device, timeline, &value); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkGetSemaphoreCounterValue", result));
    }
    return value;
}

core::Result<void> Device::Impl::WaitForValue(std::uint64_t value) const {
    if (value == 0) {
        return {};
    }
    const VkSemaphoreWaitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &timeline,
        .pValues = &value,
    };
    if (const VkResult result = vkWaitSemaphores(device, &waitInfo, std::numeric_limits<std::uint64_t>::max());
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkWaitSemaphores", result));
    }
    return {};
}

void Device::Impl::Defer(std::function<void()> destroy) {
    deferred.push_back({NextTimelineValue(), std::move(destroy)});
}

void Device::Impl::CollectGarbage(std::uint64_t completed) {
    while (!deferred.empty() && deferred.front().retireValue <= completed) {
        deferred.front().destroy();
        deferred.pop_front();
    }
}

core::Result<Device> Device::Create(const DeviceConfig& config) {
    if (s_deviceExists.exchange(true)) {
        return core::MakeError(core::ErrorCode::InvalidState, std::string(kModule), "A Device already exists");
    }

    auto impl = std::make_unique<Impl>();
    core::Result<void> created = impl->CreateInstance(config)
                                     .and_then([&] { return impl->SelectPhysicalDevice(config); })
                                     .and_then([&] { return impl->CreateLogicalDevice(config); })
                                     .and_then([&] { return impl->CreateAllocator(); })
                                     .and_then([&] { return impl->CreateFrameResources(); })
                                     .and_then([&] { return impl->CreateGlobalDescriptors(); });
    if (!created) {
        return tl::unexpected(std::move(created.error()));
    }
    DAI_LOG_INFO(kModule, "Vulkan device '{}', validation {}", impl->info.name,
                 impl->info.validationEnabled ? "on" : "off");
    return Device(std::move(impl));
}

Device::Device(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Device::Device(Device&& other) noexcept = default;
Device& Device::operator=(Device&& other) noexcept = default;

Device::~Device() {
    if (m_impl) {
        DAI_ASSERT(m_impl->swapchains == 0, "Every Swapchain must be destroyed before its Device");
        DAI_ASSERT(!m_impl->frameOpen, "A frame was begun and never ended");
    }
}

const DeviceInfo& Device::Info() const {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    return m_impl->info;
}

std::uint64_t Device::ValidationErrorCount() const {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    return m_impl->validationErrors.load();
}

std::uint64_t Device::PresentedFrameCount() const {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    return m_impl->presentedFrames;
}

std::size_t Device::PendingDestructionCount() const {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    return m_impl->deferred.size();
}

core::Result<std::optional<Frame>> Device::BeginFrame(Swapchain* swapchain) {
    DAI_PROFILE_SCOPE("rhi::BeginFrame");
    DAI_ASSERT(m_impl, "Device used after being moved from");
    DAI_ASSERT(!m_impl->frameOpen, "BeginFrame called twice without EndFrame");

    Impl::FrameSlot& slot = m_impl->frames[m_impl->frameIndex];
    if (core::Result<void> waited = m_impl->WaitForValue(slot.submittedValue); !waited) {
        return tl::unexpected(std::move(waited.error()));
    }
    core::Result<std::uint64_t> completed = m_impl->CompletedValue();
    if (!completed) {
        return tl::unexpected(std::move(completed.error()));
    }
    m_impl->CollectGarbage(*completed);

    Frame frame{CommandList(slot.recording), TextureHandle{}, Extent{}};
    if (swapchain != nullptr) {
        core::Result<TextureHandle> image = swapchain->m_impl->Acquire(m_impl->frameIndex);
        if (!image) {
            return tl::unexpected(std::move(image.error()));
        }
        if (image->IsNull()) {
            return std::optional<Frame>{};
        }
        frame.backBuffer = *image;
        frame.extent = swapchain->m_impl->extent;
    }

    if (const VkResult result = vkResetCommandPool(m_impl->device, slot.pool, 0); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkResetCommandPool", result));
    }
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (const VkResult result = vkBeginCommandBuffer(slot.recording.commands, &beginInfo); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkBeginCommandBuffer", result));
    }
    m_impl->frameOpen = true;
    return std::optional<Frame>{frame};
}

core::Result<void> Device::EndFrame(Swapchain* swapchain) {
    DAI_PROFILE_SCOPE("rhi::EndFrame");
    DAI_ASSERT(m_impl, "Device used after being moved from");
    DAI_ASSERT(m_impl->frameOpen, "EndFrame called without BeginFrame");

    Impl::FrameSlot& slot = m_impl->frames[m_impl->frameIndex];
    m_impl->frameOpen = false;

    VkSemaphoreSubmitInfo waitImage{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    std::array<VkSemaphoreSubmitInfo, 2> signals{};
    const std::uint64_t value = m_impl->NextTimelineValue();
    signals[0] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                  .semaphore = m_impl->timeline,
                  .value = value,
                  .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    std::uint32_t signalCount = 1;

    if (swapchain != nullptr) {
        Swapchain::Impl& target = *swapchain->m_impl;
        SwapchainImage& image = target.images[target.acquiredImage];
        CommandList(slot.recording).Transition(image.texture, TextureState::Present);

        waitImage.semaphore = target.acquiredSemaphore;
        waitImage.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        signals[1] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                      .semaphore = image.renderFinished,
                      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        signalCount = 2;
    }

    if (const VkResult result = vkEndCommandBuffer(slot.recording.commands); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkEndCommandBuffer", result));
    }

    const VkCommandBufferSubmitInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = slot.recording.commands,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = (swapchain != nullptr) ? 1U : 0U,
        .pWaitSemaphoreInfos = &waitImage,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
        .signalSemaphoreInfoCount = signalCount,
        .pSignalSemaphoreInfos = signals.data(),
    };
    if (const VkResult result = vkQueueSubmit2(m_impl->queue, 1, &submitInfo, VK_NULL_HANDLE); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkQueueSubmit2", result));
    }
    m_impl->submittedValue = value;
    slot.submittedValue = value;
    m_impl->frameIndex = (m_impl->frameIndex + 1) % kFramesInFlight;

    return (swapchain != nullptr) ? swapchain->m_impl->Present() : core::Result<void>{};
}

core::Result<void> Device::Submit(const std::function<void(CommandList&)>& record) {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    // A frame being recorded expects the next timeline value, which this submission would take.
    DAI_ASSERT(!m_impl->frameOpen, "Device::Submit is not allowed between BeginFrame and EndFrame");

    const VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_impl->immediatePool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    detail::CommandRecording recording{m_impl.get(), VK_NULL_HANDLE};
    if (const VkResult result = vkAllocateCommandBuffers(m_impl->device, &allocateInfo, &recording.commands);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkAllocateCommandBuffers", result));
    }
    const VkDevice device = m_impl->device;
    const VkCommandPool pool = m_impl->immediatePool;
    const VkCommandBuffer commands = recording.commands;
    struct Release {
        VkDevice device;
        VkCommandPool pool;
        VkCommandBuffer commands;
        ~Release() { vkFreeCommandBuffers(device, pool, 1, &commands); }
    };
    const Release release{device, pool, commands};

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (const VkResult result = vkBeginCommandBuffer(commands, &beginInfo); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkBeginCommandBuffer", result));
    }
    CommandList list(recording);
    record(list);
    if (const VkResult result = vkEndCommandBuffer(commands); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkEndCommandBuffer", result));
    }

    const std::uint64_t value = m_impl->NextTimelineValue();
    const VkCommandBufferSubmitInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_impl->timeline,
        .value = value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    if (const VkResult result = vkQueueSubmit2(m_impl->queue, 1, &submitInfo, VK_NULL_HANDLE); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkQueueSubmit2", result));
    }
    m_impl->submittedValue = value;
    return m_impl->WaitForValue(value);
}

core::Result<void> Device::WaitIdle() {
    DAI_ASSERT(m_impl, "Device used after being moved from");
    DAI_ASSERT(!m_impl->frameOpen, "WaitIdle is not allowed between BeginFrame and EndFrame");
    if (const VkResult result = vkDeviceWaitIdle(m_impl->device); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkDeviceWaitIdle", result));
    }
    m_impl->CollectGarbage(std::numeric_limits<std::uint64_t>::max());
    return {};
}

} // namespace daidalos::rhi
