// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/rhi/Device.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <utility>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

VkBufferUsageFlags ToVulkanUsage(BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    flags |= HasFlag(usage, BufferUsage::Vertex) ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::Index) ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::TransferSrc) ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::TransferDst) ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::Storage) ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::Indirect) ? VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : 0U;
    flags |= HasFlag(usage, BufferUsage::AccelerationStructureInput)
                 ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                 : 0U;
    return flags;
}

VkImageUsageFlags ToVulkanUsage(TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    flags |= HasFlag(usage, TextureUsage::Sampled) ? VK_IMAGE_USAGE_SAMPLED_BIT : 0U;
    flags |= HasFlag(usage, TextureUsage::ColorTarget) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0U;
    flags |= HasFlag(usage, TextureUsage::DepthTarget) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0U;
    flags |= HasFlag(usage, TextureUsage::TransferSrc) ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0U;
    flags |= HasFlag(usage, TextureUsage::TransferDst) ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0U;
    flags |= HasFlag(usage, TextureUsage::Storage) ? VK_IMAGE_USAGE_STORAGE_BIT : 0U;
    return flags;
}

} // namespace

std::uint32_t BytesPerPixel(Format format) {
    switch (format) {
    case Format::Undefined:
    case Format::Bc5Unorm:
    case Format::Bc7Unorm:
    case Format::Bc7Srgb:
        return 0;
    case Format::R8Unorm:
        return 1;
    case Format::R16Float:
        return 2;
    case Format::Rgba8Unorm:
    case Format::Rgba8Srgb:
    case Format::Bgra8Unorm:
    case Format::Bgra8Srgb:
    case Format::A2b10g10r10Unorm:
    case Format::Rg16Float:
    case Format::R32Float:
    case Format::R32Uint:
    case Format::D32Float:
        return 4;
    case Format::Rg32Float:
    case Format::Rg32Uint:
    case Format::Rgba16Float:
        return 8;
    case Format::Rgb32Float:
        return 12;
    case Format::Rgba32Float:
        return 16;
    }
    return 0;
}

Extent MipExtent(Extent extent, std::uint32_t level) {
    return {std::max(extent.width >> level, 1U), std::max(extent.height >> level, 1U)};
}

std::uint64_t MipByteSize(Format format, Extent extent) {
    switch (format) {
    case Format::Bc5Unorm:
    case Format::Bc7Unorm:
    case Format::Bc7Srgb: {
        constexpr std::uint64_t kBytesPerBlock = 16;
        const std::uint64_t blocksWide = (std::uint64_t{extent.width} + 3) / 4;
        const std::uint64_t blocksHigh = (std::uint64_t{extent.height} + 3) / 4;
        return blocksWide * blocksHigh * kBytesPerBlock;
    }
    default:
        return std::uint64_t{extent.width} * extent.height * BytesPerPixel(format);
    }
}

VkFormat ToVulkan(Format format) {
    switch (format) {
    case Format::Undefined:
        return VK_FORMAT_UNDEFINED;
    case Format::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::Bgra8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::A2b10g10r10Unorm:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::Rg32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case Format::Rgb32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Rg16Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case Format::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case Format::R16Float:
        return VK_FORMAT_R16_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    case Format::Rg32Uint:
        return VK_FORMAT_R32G32_UINT;
    case Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::Bc5Unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::Bc7Unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::Bc7Srgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

Format FromVulkan(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return Format::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return Format::Bgra8Srgb;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return Format::A2b10g10r10Unorm;
    default:
        return Format::Undefined;
    }
}

core::Result<BufferHandle> Device::CreateBuffer(const BufferDesc& desc) {
    DAI_ASSERT(desc.size > 0, "A buffer cannot be empty");

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = ToVulkanUsage(desc.usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    if (desc.memory == MemoryLocation::CpuToGpu) {
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else if (desc.memory == MemoryLocation::GpuToCpu) {
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    const bool storage = HasFlag(desc.usage, BufferUsage::Storage);
    if (storage && m_impl->freeStorageIndices.empty()) {
        return core::MakeError(
            core::ErrorCode::OutOfMemory, kModule,
            std::format("Buffer '{}': all {} storage buffer slots are in use", desc.name, kMaxBindlessBuffers));
    }

    BufferData data;
    data.usage = desc.usage;
    data.size = desc.size;
    VmaAllocationInfo allocated{};
    if (const VkResult result = vmaCreateBuffer(m_impl->allocator, &bufferInfo, &allocationInfo, &data.buffer,
                                                &data.allocation, &allocated);
        result != VK_SUCCESS) {
        return tl::unexpected(
            VulkanError(std::format("Creating buffer '{}' of {} bytes", desc.name, desc.size), result));
    }
    if (allocated.pMappedData != nullptr) {
        data.mapped = std::span(static_cast<std::byte*>(allocated.pMappedData), desc.size);
    }
    if (storage) {
        data.storageIndex = m_impl->freeStorageIndices.back();
        m_impl->freeStorageIndices.pop_back();
        const VkDescriptorBufferInfo descriptor{.buffer = data.buffer, .range = VK_WHOLE_SIZE};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_impl->globalSet,
            .dstBinding = 2,
            .dstArrayElement = data.storageIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &descriptor,
        };
        vkUpdateDescriptorSets(m_impl->device, 1, &write, 0, nullptr);
    }
    m_impl->SetName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(data.buffer), desc.name);
    return m_impl->buffers.Insert(data);
}

void Device::DestroyBuffer(BufferHandle buffer) {
    if (const std::optional<BufferData> data = m_impl->buffers.Remove(buffer)) {
        // The storage slot is released with the buffer: frames in flight may still read it.
        Impl* impl = m_impl.get();
        m_impl->Defer([impl, data = *data] {
            vmaDestroyBuffer(impl->allocator, data.buffer, data.allocation);
            if (HasFlag(data.usage, BufferUsage::Storage)) {
                impl->freeStorageIndices.push_back(data.storageIndex);
            }
        });
    }
}

std::span<std::byte> Device::Mapped(BufferHandle buffer) {
    const BufferData& data = m_impl->buffers.Get(buffer);
    DAI_ASSERT(!data.mapped.empty(), "The buffer is not in CPU-visible memory");
    return data.mapped;
}

core::Result<void> Device::UploadBuffer(BufferHandle buffer, std::span<const std::byte> data) {
    DAI_ASSERT(m_impl->buffers.Get(buffer).size >= data.size(), "The data does not fit in the buffer");
    DAI_ASSERT(HasFlag(m_impl->buffers.Get(buffer).usage, BufferUsage::TransferDst),
               "The buffer needs BufferUsage::TransferDst");

    DAI_TRY_ASSIGN(BufferHandle staging, CreateBuffer({.size = data.size(),
                                                       .usage = BufferUsage::TransferSrc,
                                                       .memory = MemoryLocation::CpuToGpu,
                                                       .name = "Staging"}));
    std::memcpy(Mapped(staging).data(), data.data(), data.size());
    core::Result<CommandList> commands = m_impl->UploadCommands();
    if (commands) {
        // Waits for earlier uses of the buffer, an earlier upload in the same batch included.
        const BufferUse use{buffer, Access::TransferWrite};
        commands->Use({}, {&use, 1});
        commands->CopyBuffer(staging, buffer, data.size());
    }
    DestroyBuffer(staging); // Released once the copy is done.
    return commands ? core::Result<void>{} : tl::unexpected(std::move(commands.error()));
}

core::Result<TextureHandle> Device::CreateTexture(const TextureDesc& desc) {
    DAI_ASSERT(desc.extent.width > 0 && desc.extent.height > 0, "A texture cannot be empty");
    DAI_ASSERT(desc.format != Format::Undefined, "A texture needs a format");
    DAI_ASSERT(desc.mipLevels > 0, "A texture has at least one mip level");

    const bool sampled = HasFlag(desc.usage, TextureUsage::Sampled);
    const bool storage = HasFlag(desc.usage, TextureUsage::Storage);
    DAI_ASSERT(!storage || desc.format == Format::Rgba32Float || desc.format == Format::Rgba16Float ||
                   desc.format == Format::Rgba8Unorm || desc.format == Format::Rg16Float ||
                   desc.format == Format::R8Unorm || desc.format == Format::R16Float ||
                   desc.format == Format::R32Float || desc.format == Format::R32Uint,
               "A storage texture has a format that shaders declare their storage images with");
    if (storage && m_impl->freeStorageImageIndices.empty()) {
        return core::MakeError(
            core::ErrorCode::OutOfMemory, kModule,
            std::format("Texture '{}': all {} storage image slots are in use", desc.name, kMaxStorageImages));
    }
    DAI_ASSERT(!desc.cube ||
                   (desc.extent.width == desc.extent.height && !storage &&
                    !HasFlag(desc.usage, TextureUsage::ColorTarget) && !HasFlag(desc.usage, TextureUsage::DepthTarget)),
               "A cube has square faces, and is only sampled and copied to");
    const bool volume = desc.depth > 1;
    DAI_ASSERT(!volume || (!desc.cube && desc.mipLevels == 1 && !HasFlag(desc.usage, TextureUsage::ColorTarget) &&
                           !HasFlag(desc.usage, TextureUsage::DepthTarget)),
               "A 3D texture has one mip level, and is only sampled and stored");
    std::vector<std::uint32_t>& freeSampledIndices = desc.cube ? m_impl->freeCubeIndices : m_impl->freeBindlessIndices;
    if (sampled && freeSampledIndices.empty()) {
        return core::MakeError(
            core::ErrorCode::OutOfMemory, kModule,
            std::format("Texture '{}': all {} bindless slots are in use", desc.name, kMaxBindlessTextures));
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = desc.cube ? VkImageCreateFlags{VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT} : VkImageCreateFlags{0},
        .imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
        .format = ToVulkan(desc.format),
        .extent = {desc.extent.width, desc.extent.height, std::max(desc.depth, 1U)},
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.cube ? 6U : 1U,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = ToVulkanUsage(desc.usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO};

    TextureData data;
    data.extent = desc.extent;
    data.mipLevels = desc.mipLevels;
    data.layers = imageInfo.arrayLayers;
    data.depth = imageInfo.extent.depth;
    data.format = desc.format;
    data.usage = desc.usage;
    if (const VkResult result =
            vmaCreateImage(m_impl->allocator, &imageInfo, &allocationInfo, &data.image, &data.allocation, nullptr);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating texture '{}'", desc.name), result));
    }

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = data.image,
        .viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE
                    : volume  ? VK_IMAGE_VIEW_TYPE_3D
                              : VK_IMAGE_VIEW_TYPE_2D,
        .format = imageInfo.format,
        .subresourceRange = {.aspectMask = (desc.format == Format::D32Float)
                                               ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT}
                                               : VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT},
                             .levelCount = desc.mipLevels,
                             .layerCount = imageInfo.arrayLayers},
    };
    if (const VkResult result = vkCreateImageView(m_impl->device, &viewInfo, nullptr, &data.view);
        result != VK_SUCCESS) {
        vmaDestroyImage(m_impl->allocator, data.image, data.allocation);
        return tl::unexpected(VulkanError(std::format("Creating a view of texture '{}'", desc.name), result));
    }

    if (sampled) {
        data.bindlessIndex = freeSampledIndices.back();
        freeSampledIndices.pop_back();
        const VkDescriptorImageInfo descriptor{
            .imageView = data.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_impl->globalSet,
            .dstBinding = desc.cube ? 5U : 0U,
            .dstArrayElement = data.bindlessIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &descriptor,
        };
        vkUpdateDescriptorSets(m_impl->device, 1, &write, 0, nullptr);
    }
    if (storage) {
        data.storageIndex = m_impl->freeStorageImageIndices.back();
        m_impl->freeStorageImageIndices.pop_back();
        const VkDescriptorImageInfo descriptor{.imageView = data.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_impl->globalSet,
            .dstBinding = 4,
            .dstArrayElement = data.storageIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptor,
        };
        vkUpdateDescriptorSets(m_impl->device, 1, &write, 0, nullptr);
    }

    m_impl->SetName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(data.image), desc.name);
    return m_impl->textures.Insert(data);
}

void Device::DestroyTexture(TextureHandle texture) {
    if (const std::optional<TextureData> data = m_impl->textures.Remove(texture)) {
        DAI_ASSERT(data->allocation != nullptr, "Swapchain images belong to the swapchain");
        // The bindless slot is released with the image: frames in flight may still read it.
        Impl* impl = m_impl.get();
        m_impl->Defer([impl, data = *data] {
            vkDestroyImageView(impl->device, data.view, nullptr);
            vmaDestroyImage(impl->allocator, data.image, data.allocation);
            if (HasFlag(data.usage, TextureUsage::Sampled)) {
                (data.layers == 6 ? impl->freeCubeIndices : impl->freeBindlessIndices).push_back(data.bindlessIndex);
            }
            if (HasFlag(data.usage, TextureUsage::Storage)) {
                impl->freeStorageImageIndices.push_back(data.storageIndex);
            }
        });
    }
}

core::Result<void> Device::UploadTexture(TextureHandle texture, std::span<const std::byte> pixels) {
    const TextureData& data = m_impl->textures.Get(texture);
    DAI_ASSERT(data.depth == 1, "3D textures are never uploaded");
    std::uint64_t expectedSize = 0;
    for (std::uint32_t level = 0; level < data.mipLevels; ++level) {
        expectedSize += MipByteSize(data.format, MipExtent(data.extent, level));
    }
    expectedSize *= data.layers;
    DAI_ASSERT(pixels.size() == expectedSize, "The pixels do not match the size of the texture");

    DAI_TRY_ASSIGN(BufferHandle staging, CreateBuffer({.size = pixels.size(),
                                                       .usage = BufferUsage::TransferSrc,
                                                       .memory = MemoryLocation::CpuToGpu,
                                                       .name = "Staging"}));
    std::memcpy(Mapped(staging).data(), pixels.data(), pixels.size());
    core::Result<CommandList> commands = m_impl->UploadCommands();
    if (commands) {
        commands->Transition(texture, TextureState::TransferDst);
        commands->CopyBufferToTexture(staging, texture);
        commands->Transition(texture, TextureState::ShaderRead);
    }
    DestroyBuffer(staging); // Released once the copy is done.
    return commands ? core::Result<void>{} : tl::unexpected(std::move(commands.error()));
}

std::uint32_t Device::StorageIndex(BufferHandle buffer) const {
    const BufferData& data = m_impl->buffers.Get(buffer);
    DAI_ASSERT(HasFlag(data.usage, BufferUsage::Storage), "The buffer is not a storage buffer");
    return data.storageIndex;
}

std::uint32_t Device::StorageImageIndex(TextureHandle texture) const {
    const TextureData& data = m_impl->textures.Get(texture);
    DAI_ASSERT(HasFlag(data.usage, TextureUsage::Storage), "The texture is not a storage texture");
    return data.storageIndex;
}

std::uint32_t Device::BindlessIndex(TextureHandle texture) const {
    const TextureData& data = m_impl->textures.Get(texture);
    DAI_ASSERT(HasFlag(data.usage, TextureUsage::Sampled), "The texture is not sampled");
    return data.bindlessIndex;
}

core::Result<PipelineHandle> Device::CreatePipeline(const GraphicsPipelineDesc& desc) {

    const auto createModule = [this](std::span<const std::uint32_t> code, VkShaderModule& module) {
        const VkShaderModuleCreateInfo moduleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size_bytes(),
            .pCode = code.data(),
        };
        return vkCreateShaderModule(m_impl->device, &moduleInfo, nullptr, &module);
    };
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    const auto destroyModules = [&] {
        vkDestroyShaderModule(m_impl->device, vertexModule, nullptr);
        vkDestroyShaderModule(m_impl->device, fragmentModule, nullptr);
    };
    if (const VkResult result = createModule(desc.vertexShader, vertexModule); result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating the vertex shader of '{}'", desc.name), result));
    }
    if (const VkResult result = createModule(desc.fragmentShader, fragmentModule); result != VK_SUCCESS) {
        destroyModules();
        return tl::unexpected(VulkanError(std::format("Creating the fragment shader of '{}'", desc.name), result));
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertexModule,
         .pName = "VSMain"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragmentModule,
         .pName = "PSMain"},
    }};

    const VkVertexInputBindingDescription binding{
        .binding = 0, .stride = desc.vertexStride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.vertexAttributes.size());
    for (const VertexAttribute& attribute : desc.vertexAttributes) {
        attributes.push_back({attribute.location, 0, ToVulkan(attribute.format), attribute.offset});
    }
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = attributes.empty() ? 0U : 1U,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    if (desc.cull == CullMode::Back) {
        cullMode = VK_CULL_MODE_BACK_BIT;
    } else if (desc.cull == CullMode::Front) {
        cullMode = VK_CULL_MODE_FRONT_BIT;
    }
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cullMode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = (desc.depthBias != 0.0f || desc.depthBiasSlope != 0.0f) ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = desc.depthBias,
        .depthBiasSlopeFactor = desc.depthBiasSlope,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const bool hasDepth = desc.depthFormat != Format::Undefined;
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = (hasDepth && desc.depthTest) ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (hasDepth && desc.depthWrite) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
    };
    const bool hasColor = desc.colorFormat != Format::Undefined;
    DAI_ASSERT(hasColor || desc.moreColorFormats.empty(), "Further color targets need a first one");
    DAI_ASSERT(desc.moreColorFormats.size() < kMaxColorTargets, "Too many color targets");
    const auto colorCount = hasColor ? static_cast<std::uint32_t>(desc.moreColorFormats.size() + 1) : 0U;
    VkPipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = (desc.blend != BlendMode::Opaque) ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor =
            (desc.blend == BlendMode::Premultiplied) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    std::array<VkPipelineColorBlendAttachmentState, kMaxColorTargets> blendAttachments{};
    blendAttachments.fill(blendAttachment);
    const VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = colorCount,
        .pAttachments = blendAttachments.data(),
    };
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    std::array<VkFormat, kMaxColorTargets> colorFormats{};
    colorFormats[0] = ToVulkan(desc.colorFormat);
    for (std::size_t index = 0; index < desc.moreColorFormats.size(); ++index) {
        colorFormats[index + 1] = ToVulkan(desc.moreColorFormats[index]);
    }
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = colorCount,
        .pColorAttachmentFormats = colorFormats.data(),
        .depthAttachmentFormat = ToVulkan(desc.depthFormat),
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamicState,
        .layout = m_impl->pipelineLayout,
    };

    PipelineData data;
    const auto start = std::chrono::steady_clock::now();
    const VkResult result =
        vkCreateGraphicsPipelines(m_impl->device, m_impl->pipelineCache, 1, &pipelineInfo, nullptr, &data.pipeline);
    m_impl->NotePipeline(desc.name, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    destroyModules();
    if (result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating pipeline '{}'", desc.name), result));
    }
    m_impl->SetName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(data.pipeline), desc.name);
    return m_impl->pipelines.Insert(data);
}

core::Result<PipelineHandle> Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
    const VkShaderModuleCreateInfo moduleInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = desc.shader.size_bytes(),
        .pCode = desc.shader.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateShaderModule(m_impl->device, &moduleInfo, nullptr, &module);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating the compute shader of '{}'", desc.name), result));
    }
    const VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = module,
                  .pName = "CSMain"},
        .layout = m_impl->pipelineLayout,
    };
    PipelineData data{.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE};
    const auto start = std::chrono::steady_clock::now();
    const VkResult result =
        vkCreateComputePipelines(m_impl->device, m_impl->pipelineCache, 1, &pipelineInfo, nullptr, &data.pipeline);
    m_impl->NotePipeline(desc.name, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    vkDestroyShaderModule(m_impl->device, module, nullptr);
    if (result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating compute pipeline '{}'", desc.name), result));
    }
    m_impl->SetName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(data.pipeline), desc.name);
    return m_impl->pipelines.Insert(data);
}

void Device::DestroyPipeline(PipelineHandle pipeline) {
    if (const std::optional<PipelineData> data = m_impl->pipelines.Remove(pipeline)) {
        m_impl->Defer([device = m_impl->device, data = *data] { vkDestroyPipeline(device, data.pipeline, nullptr); });
    }
}

} // namespace daidalos::rhi
