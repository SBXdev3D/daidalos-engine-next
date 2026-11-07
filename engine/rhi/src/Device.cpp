// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/rhi/Device.hpp>

#include <algorithm>
#include <array>
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

// The device extensions ray tracing needs; deferred host operations is required by acceleration structures.
constexpr std::array<const char*, 3> kRayTracingExtensions{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                                           VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                                                           VK_KHR_RAY_QUERY_EXTENSION_NAME};

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

bool HasLayerExtension(const char* layer, const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data());
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
    if (properties.apiVersion < VK_API_VERSION_1_4) {
        return "Vulkan 1.4";
    }
    // Ray tracing is required (ADR-0028), through ray queries only: no ray tracing pipelines.
    for (const char* extension : kRayTracingExtensions) {
        if (!HasDeviceExtension(device, extension)) {
            return std::format("hardware ray tracing ({})", extension);
        }
    }

    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .pNext = &rayQuery};
    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                                .pNext = &accelerationStructure};
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
        features12.descriptorBindingStorageBufferUpdateAfterBind == VK_FALSE ||
        features12.shaderSampledImageArrayNonUniformIndexing == VK_FALSE ||
        features.features.shaderSampledImageArrayDynamicIndexing == VK_FALSE ||
        features.features.shaderStorageBufferArrayDynamicIndexing == VK_FALSE) {
        return "descriptor indexing";
    }
    if (accelerationStructure.accelerationStructure == VK_FALSE || rayQuery.rayQuery == VK_FALSE ||
        features12.bufferDeviceAddress == VK_FALSE) {
        return "hardware ray tracing (acceleration structures and ray queries)";
    }
    if (features.features.textureCompressionBC == VK_FALSE) {
        return "BC texture compression";
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
    return core::Error{code, kModule, std::format("{} failed with VkResult {}", operation, static_cast<int>(result))};
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
        vkDestroyQueryPool(device, timestamps, nullptr);
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
        return core::MakeError(core::ErrorCode::NotSupported, kModule,
                               "No Vulkan loader found: install a graphics driver with Vulkan support");
    }

    std::uint32_t version = 0;
    if (vkEnumerateInstanceVersion == nullptr || vkEnumerateInstanceVersion(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_4) {
        return core::MakeError(core::ErrorCode::NotSupported, kModule,
                               "The Vulkan loader does not support Vulkan 1.4: update the graphics driver");
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

    // Synchronization validation is a feature of the validation layer, switched on by this extension.
    const VkValidationFeatureEnableEXT synchronization = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    const VkValidationFeaturesEXT validationFeatures{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = useMessenger ? &messengerInfo : nullptr,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures = &synchronization,
    };
    const bool validateSynchronization = config.synchronizationValidation && !layers.empty() &&
                                         HasLayerExtension(kValidationLayer, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    if (validateSynchronization) {
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    } else if (config.synchronizationValidation) {
        DAI_LOG_WARNING(kModule, "Synchronization validation was requested but is not available");
    }

    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = config.applicationName.c_str(),
        .pEngineName = "Daidalos Engine Next",
        .apiVersion = VK_API_VERSION_1_4,
    };
    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validateSynchronization ? static_cast<const void*>(&validationFeatures)
                 : useMessenger          ? static_cast<const void*>(&messengerInfo)
                                         : nullptr,
        .pApplicationInfo = &application,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance); result != VK_SUCCESS) {
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            return core::MakeError(core::ErrorCode::NotSupported, kModule, "No Vulkan 1.4 driver is available");
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
        return core::MakeError(core::ErrorCode::NotSupported, kModule, "No Vulkan device found");
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
            timestampPeriodMs = static_cast<double>(properties.limits.timestampPeriod) / 1.0e6;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        return core::MakeError(core::ErrorCode::NotSupported, kModule,
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

    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = VK_TRUE,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &rayQuery,
        .accelerationStructure = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &accelerationStructure,
        .shaderDemoteToHelperInvocation = VK_TRUE, // What discard in a shader compiles to.
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE, // Acceleration structures are built from buffer addresses.
    };
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    const bool anisotropic = config.anisotropy > 1.0f && supported.samplerAnisotropy == VK_TRUE;
    info.anisotropy = anisotropic ? std::min(config.anisotropy, properties.limits.maxSamplerAnisotropy) : 1.0f;
    const VkSampleCountFlags sampleCounts =
        properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
    for (const std::uint32_t samples : {8U, 4U, 2U}) {
        if ((sampleCounts & samples) != 0) {
            info.maxSamples = samples;
            break;
        }
    }

    const VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features12,
        .features = {.samplerAnisotropy = anisotropic ? VK_TRUE : VK_FALSE,
                     .textureCompressionBC = VK_TRUE,
                     .shaderSampledImageArrayDynamicIndexing = VK_TRUE,
                     .shaderStorageBufferArrayDynamicIndexing = VK_TRUE},
    };

    std::vector<const char*> extensions(kRayTracingExtensions.begin(), kRayTracingExtensions.end());
    if (config.presentation) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
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
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physicalDevice,
        .device = device,
        .pVulkanFunctions = &functions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
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
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    if (families[queueFamily].timestampValidBits > 0 && timestampPeriodMs > 0.0) {
        const VkQueryPoolCreateInfo queryInfo{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = kMaxTimestamps * kFramesInFlight,
        };
        if (const VkResult result = vkCreateQueryPool(device, &queryInfo, nullptr, &timestamps); result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkCreateQueryPool", result));
        }
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
        if (timestamps != VK_NULL_HANDLE) {
            frame.recording.firstTimestamp = static_cast<std::uint32_t>(&frame - frames.data()) * kMaxTimestamps;
        }
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
            // Sharp textures on surfaces seen at a grazing angle, such as floors.
            .anisotropyEnable = (filter == VK_FILTER_LINEAR && info.anisotropy > 1.0f) ? VK_TRUE : VK_FALSE,
            .maxAnisotropy = info.anisotropy,
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

    const VkSamplerCreateInfo shadowInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
    };
    if (const VkResult result = vkCreateSampler(device, &shadowInfo, nullptr,
                                                &samplers[static_cast<std::size_t>(SamplerIndex::ShadowCompare)]);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkCreateSampler", result));
    }

    const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = kMaxBindlessTextures,
         .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
         .descriptorCount = static_cast<std::uint32_t>(samplers.size()),
         .stageFlags = VK_SHADER_STAGE_ALL,
         .pImmutableSamplers = samplers.data()},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = kMaxBindlessBuffers,
         .stageFlags = VK_SHADER_STAGE_ALL},
    }};
    constexpr VkDescriptorBindingFlags kBindlessFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    const std::array<VkDescriptorBindingFlags, 3> bindingFlags{kBindlessFlags, 0, kBindlessFlags};
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

    const std::array<VkDescriptorPoolSize, 3> poolSizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxBindlessTextures},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxBindlessBuffers},
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
        .stageFlags = VK_SHADER_STAGE_ALL,
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
    freeStorageIndices.reserve(kMaxBindlessBuffers);
    for (std::uint32_t index = kMaxBindlessBuffers; index > 0; --index) {
        freeStorageIndices.push_back(index - 1);
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

core::Result<void> Device::Impl::ReadTimestamps(detail::CommandRecording& recording) {
    const std::uint32_t count = std::exchange(recording.timestampCount, 0U);
    if (!recording.firstTimestamp.has_value() || count == 0) {
        return {};
    }
    // The slot's last frame is finished, so the results are there without waiting.
    std::array<std::uint64_t, kMaxTimestamps> ticks{};
    if (const VkResult result =
            vkGetQueryPoolResults(device, timestamps, *recording.firstTimestamp, count, count * sizeof(std::uint64_t),
                                  ticks.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkGetQueryPoolResults", result));
    }
    completedTimestamps.resize(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        completedTimestamps[index] = static_cast<double>(ticks[index] - ticks[0]) * timestampPeriodMs;
    }
    return {};
}

core::Result<std::uint64_t> Device::Impl::SubmitCommands(VkCommandBuffer commands) {
    const std::uint64_t value = NextTimelineValue();
    const VkCommandBufferSubmitInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = timeline,
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
    if (const VkResult result = vkQueueSubmit2(queue, 1, &submitInfo, VK_NULL_HANDLE); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkQueueSubmit2", result));
    }
    submittedValue = value;
    return value;
}

core::Result<CommandList> Device::Impl::UploadCommands() {
    DAI_ASSERT(!frameOpen, "Uploads are not allowed between BeginFrame and EndFrame");
    if (uploads.commands == VK_NULL_HANDLE) {
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = immediatePool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer commands = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(device, &allocateInfo, &commands); result != VK_SUCCESS) {
            return tl::unexpected(VulkanError("vkAllocateCommandBuffers", result));
        }
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (const VkResult result = vkBeginCommandBuffer(commands, &beginInfo); result != VK_SUCCESS) {
            vkFreeCommandBuffers(device, immediatePool, 1, &commands);
            return tl::unexpected(VulkanError("vkBeginCommandBuffer", result));
        }
        uploads = {this, commands};
    }
    return CommandList(uploads);
}

core::Result<void> Device::Impl::FlushUploads() {
    if (uploads.commands == VK_NULL_HANDLE) {
        return {};
    }
    const VkCommandBuffer commands = std::exchange(uploads.commands, VK_NULL_HANDLE);
    // Released once the copies are done, like the staging buffers they read.
    Defer([this, commands] { vkFreeCommandBuffers(device, immediatePool, 1, &commands); });

    // Later submissions on this queue read what the copies wrote: vertices, indices, storage buffers.
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
    if (const VkResult result = vkEndCommandBuffer(commands); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkEndCommandBuffer", result));
    }
    DAI_TRY(SubmitCommands(commands));
    return {};
}

core::Result<Device> Device::Create(const DeviceConfig& config) {
    if (s_deviceExists.exchange(true)) {
        return core::MakeError(core::ErrorCode::InvalidState, kModule, "A Device already exists");
    }

    auto impl = std::make_unique<Impl>();
    DAI_TRY(impl->CreateInstance(config));
    DAI_TRY(impl->SelectPhysicalDevice(config));
    DAI_TRY(impl->CreateLogicalDevice(config));
    DAI_TRY(impl->CreateAllocator());
    DAI_TRY(impl->CreateFrameResources());
    DAI_TRY(impl->CreateGlobalDescriptors());
    DAI_LOG_INFO(kModule, "Vulkan device '{}', validation {}, anisotropy {}x", impl->info.name,
                 impl->info.validationEnabled ? "on" : "off", impl->info.anisotropy);
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
    return m_impl->info;
}

std::uint64_t Device::ValidationErrorCount() const {
    return m_impl->validationErrors.load();
}

std::span<const double> Device::CompletedTimestamps() const {
    return m_impl->completedTimestamps;
}

std::uint64_t Device::PresentedFrameCount() const {
    return m_impl->presentedFrames;
}

std::size_t Device::PendingDestructionCount() const {
    return m_impl->deferred.size();
}

MemoryStats Device::Memory() const {
    MemoryStats stats;
    stats.buffers = m_impl->buffers.Size();
    stats.textures = m_impl->textures.Size();
    stats.pipelines = m_impl->pipelines.Size();

    const VkPhysicalDeviceMemoryProperties* properties = nullptr;
    vmaGetMemoryProperties(m_impl->allocator, &properties);
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(m_impl->allocator, budgets.data());
    for (std::uint32_t heap = 0; heap < properties->memoryHeapCount; ++heap) {
        if ((properties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            stats.usedBytes += budgets[heap].usage;
            stats.budgetBytes += budgets[heap].budget;
        }
    }
    return stats;
}

core::Result<std::optional<Frame>> Device::BeginFrame(Swapchain* swapchain) {
    DAI_PROFILE_SCOPE("rhi::BeginFrame");
    DAI_ASSERT(!m_impl->frameOpen, "BeginFrame called twice without EndFrame");

    DAI_TRY(m_impl->FlushUploads());
    Impl::FrameSlot& slot = m_impl->frames[m_impl->frameIndex];
    DAI_TRY(m_impl->WaitForValue(slot.submittedValue));
    DAI_TRY_ASSIGN(std::uint64_t completed, m_impl->CompletedValue());
    m_impl->CollectGarbage(completed);
    DAI_TRY(m_impl->ReadTimestamps(slot.recording));

    Frame frame{CommandList(slot.recording), TextureHandle{}, Extent{}, m_impl->frameIndex};
    if (swapchain != nullptr) {
        DAI_TRY_ASSIGN(TextureHandle image, swapchain->m_impl->Acquire(m_impl->frameIndex));
        if (image.IsNull()) {
            return std::optional<Frame>{};
        }
        frame.backBuffer = image;
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
    if (slot.recording.firstTimestamp.has_value()) {
        vkCmdResetQueryPool(slot.recording.commands, m_impl->timestamps, *slot.recording.firstTimestamp,
                            kMaxTimestamps);
    }
    m_impl->frameOpen = true;
    return std::optional<Frame>{frame};
}

core::Result<void> Device::EndFrame(Swapchain* swapchain) {
    DAI_PROFILE_SCOPE("rhi::EndFrame");
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
    // A frame being recorded expects the next timeline value, which this submission would take.
    DAI_ASSERT(!m_impl->frameOpen, "Device::Submit is not allowed between BeginFrame and EndFrame");
    DAI_TRY(m_impl->FlushUploads());

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

    DAI_TRY_ASSIGN(const std::uint64_t value, m_impl->SubmitCommands(commands));
    return m_impl->WaitForValue(value);
}

core::Result<void> Device::WaitIdle() {
    DAI_ASSERT(!m_impl->frameOpen, "WaitIdle is not allowed between BeginFrame and EndFrame");
    DAI_TRY(m_impl->FlushUploads());
    if (const VkResult result = vkDeviceWaitIdle(m_impl->device); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError("vkDeviceWaitIdle", result));
    }
    m_impl->CollectGarbage(std::numeric_limits<std::uint64_t>::max());
    return {};
}

} // namespace daidalos::rhi
