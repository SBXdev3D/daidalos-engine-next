// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/AccelerationStructure.hpp>
#include <daidalos/rhi/CommandList.hpp>
#include <daidalos/rhi/Device.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <utility>
#include <vector>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

VkDeviceAddress AddressOf(VkDevice device, VkBuffer buffer) {
    const VkBufferDeviceAddressInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

// What a build reads: the triangles of a bottom-level structure, or the instances of a top-level one.
struct BuildGeometry {
    VkAccelerationStructureGeometryKHR geometry{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    std::uint32_t primitives = 0;
};

BuildGeometry Triangles(Device::Impl& device, const TriangleGeometry& triangles) {
    BuildGeometry result;
    result.geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // Whether a hit is opaque is the instance's choice, from its material.
    result.geometry.flags = 0;
    VkAccelerationStructureGeometryTrianglesDataKHR& data = result.geometry.geometry.triangles;
    data = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexStride = triangles.vertexStride,
            .maxVertex = triangles.vertexCount > 0 ? triangles.vertexCount - 1 : 0,
            .indexType = VK_INDEX_TYPE_UINT32};
    if (triangles.vertices) {
        data.vertexData.deviceAddress =
            AddressOf(device.device, device.buffers.Get(triangles.vertices).buffer) + triangles.vertexOffset;
    }
    if (triangles.indices) {
        data.indexData.deviceAddress =
            AddressOf(device.device, device.buffers.Get(triangles.indices).buffer) + triangles.indexOffset;
    }
    result.primitives = triangles.indexCount / 3;
    return result;
}

BuildGeometry Boxes(Device::Impl& device, const AabbGeometry& boxes) {
    BuildGeometry result;
    result.geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    result.geometry.flags = 0;
    result.geometry.geometry.aabbs = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
                                      .stride = boxes.stride};
    if (boxes.boxes) {
        result.geometry.geometry.aabbs.data.deviceAddress =
            AddressOf(device.device, device.buffers.Get(boxes.boxes).buffer) + boxes.offset;
    }
    result.primitives = boxes.count;
    return result;
}

// A bottom-level structure's geometry: its boxes, or else its triangles.
BuildGeometry BottomGeometry(Device::Impl& device, const AccelerationStructureDesc& desc) {
    return desc.boxes.count > 0 ? Boxes(device, desc.boxes) : Triangles(device, desc.geometry);
}

BuildGeometry Instances(Device::Impl& device, BufferHandle instances, std::uint32_t count) {
    BuildGeometry result;
    result.geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    result.geometry.geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                                          .arrayOfPointers = VK_FALSE};
    if (instances) {
        result.geometry.geometry.instances.data.deviceAddress =
            AddressOf(device.device, device.buffers.Get(instances).buffer);
    }
    result.primitives = count;
    return result;
}

VkAccelerationStructureBuildGeometryInfoKHR BuildInfo(const AccelerationStructureDesc& desc,
                                                      const VkAccelerationStructureGeometryKHR& geometry) {
    if (desc.refit) {
        return {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                     VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &geometry,
        };
    }
    return {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = desc.level == AccelerationStructureLevel::Top ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                                                              : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = static_cast<VkBuildAccelerationStructureFlagsKHR>(
            desc.rebuiltEveryFrame ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                                   : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR),
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
}

void RecordBuild(VkCommandBuffer commands, const AccelerationStructureData& data, const BuildGeometry& geometry) {
    VkAccelerationStructureBuildGeometryInfoKHR info = BuildInfo(data.desc, geometry.geometry);
    info.dstAccelerationStructure = data.structure;
    info.scratchData.deviceAddress = data.scratchAddress;
    const VkAccelerationStructureBuildRangeInfoKHR range{.primitiveCount = geometry.primitives};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(commands, 1, &info, &ranges);
}

} // namespace

core::Result<AccelerationStructureHandle> Device::CreateAccelerationStructure(const AccelerationStructureDesc& desc) {
    Impl& impl = *m_impl;
    const bool top = desc.level == AccelerationStructureLevel::Top;
    DAI_ASSERT(top ? desc.maxInstances > 0 : desc.geometry.indexCount >= 3 || desc.boxes.count > 0,
               "A structure needs instances, at least one triangle or a box");
    if (top && impl.freeAccelerationStructureIndices.empty()) {
        return core::MakeError(core::ErrorCode::OutOfMemory, kModule,
                               std::format("Acceleration structure '{}': all {} slots are in use", desc.name,
                                           kMaxBindlessAccelerationStructures));
    }

    const BuildGeometry geometry =
        top ? Instances(impl, BufferHandle{}, desc.maxInstances) : BottomGeometry(impl, desc);
    const VkAccelerationStructureBuildGeometryInfoKHR info = BuildInfo(desc, geometry.geometry);
    VkAccelerationStructureBuildSizesInfoKHR sizes{.sType =
                                                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(impl.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &geometry.primitives, &sizes);

    AccelerationStructureData data;
    data.desc = desc;
    data.size = sizes.accelerationStructureSize;
    const VmaAllocationCreateInfo allocation{.usage = VMA_MEMORY_USAGE_AUTO};
    VkBufferCreateInfo storageInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizes.accelerationStructureSize,
        .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    impl.Share(storageInfo);
    if (const VkResult result =
            vmaCreateBuffer(impl.allocator, &storageInfo, &allocation, &data.storage, &data.storageAllocation, nullptr);
        result != VK_SUCCESS) {
        return tl::unexpected(VulkanError(std::format("Creating acceleration structure '{}'", desc.name), result));
    }
    VkBufferCreateInfo scratchInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = std::max(sizes.buildScratchSize, sizes.updateScratchSize),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    impl.Share(scratchInfo);
    if (const VkResult result =
            vmaCreateBufferWithAlignment(impl.allocator, &scratchInfo, &allocation, impl.scratchAlignment,
                                         &data.scratch, &data.scratchAllocation, nullptr);
        result != VK_SUCCESS) {
        vmaDestroyBuffer(impl.allocator, data.storage, data.storageAllocation);
        return tl::unexpected(VulkanError(std::format("Creating the scratch memory of '{}'", desc.name), result));
    }
    data.scratchAddress = AddressOf(impl.device, data.scratch);

    const VkAccelerationStructureCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = data.storage,
        .size = sizes.accelerationStructureSize,
        .type = info.type,
    };
    if (const VkResult result = vkCreateAccelerationStructureKHR(impl.device, &createInfo, nullptr, &data.structure);
        result != VK_SUCCESS) {
        vmaDestroyBuffer(impl.allocator, data.scratch, data.scratchAllocation);
        vmaDestroyBuffer(impl.allocator, data.storage, data.storageAllocation);
        return tl::unexpected(VulkanError("vkCreateAccelerationStructureKHR", result));
    }
    const VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = data.structure,
    };
    data.address = vkGetAccelerationStructureDeviceAddressKHR(impl.device, &addressInfo);

    if (top) {
        data.bindlessIndex = impl.freeAccelerationStructureIndices.back();
        impl.freeAccelerationStructureIndices.pop_back();
        const VkWriteDescriptorSetAccelerationStructureKHR structures{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &data.structure,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &structures,
            .dstSet = impl.globalSet,
            .dstBinding = 3,
            .dstArrayElement = data.bindlessIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        };
        vkUpdateDescriptorSets(impl.device, 1, &write, 0, nullptr);
    }
    impl.SetName(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, reinterpret_cast<std::uint64_t>(data.structure), desc.name);
    impl.SetName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(data.storage), desc.name);
    return impl.accelerationStructures.Insert(data);
}

void Device::DestroyAccelerationStructure(AccelerationStructureHandle structure) {
    if (const std::optional<AccelerationStructureData> data = m_impl->accelerationStructures.Remove(structure)) {
        Impl* impl = m_impl.get();
        m_impl->Defer([impl, data = *data] { impl->DestroyAccelerationStructureNow(data); });
    }
}

void Device::Impl::DestroyAccelerationStructureNow(const AccelerationStructureData& data) {
    vkDestroyAccelerationStructureKHR(device, data.structure, nullptr);
    vmaDestroyBuffer(allocator, data.storage, data.storageAllocation);
    if (data.scratch != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, data.scratch, data.scratchAllocation);
    }
    if (data.desc.level == AccelerationStructureLevel::Top) {
        freeAccelerationStructureIndices.push_back(data.bindlessIndex);
    }
}

std::uint32_t Device::AccelerationStructureIndex(AccelerationStructureHandle structure) const {
    const AccelerationStructureData& data = m_impl->accelerationStructures.Get(structure);
    DAI_ASSERT(data.desc.level == AccelerationStructureLevel::Top, "Only top-level structures are traced");
    return data.bindlessIndex;
}

std::uint64_t Device::AccelerationStructureSize(AccelerationStructureHandle structure) const {
    return m_impl->accelerationStructures.Get(structure).size;
}

core::Result<void> Device::BuildWithUploads(AccelerationStructureHandle structure) {
    DAI_ASSERT(!m_impl->frameOpen, "BuildWithUploads is not allowed between BeginFrame and EndFrame");
    AccelerationStructureData& data = m_impl->accelerationStructures.Get(structure);
    DAI_ASSERT(data.desc.level == AccelerationStructureLevel::Bottom && data.scratch != VK_NULL_HANDLE,
               "Only a bottom-level structure that was never built this way can be built with the uploads");
    DAI_TRY_ASSIGN(CommandList commands, m_impl->UploadCommands());
    const std::array<BufferUse, 2> inputs{{{data.desc.geometry.vertices, Access::AccelerationStructureRead},
                                           {data.desc.geometry.indices, Access::AccelerationStructureRead}}};
    const AccelerationStructureUse write{structure, Access::AccelerationStructureWrite};
    commands.Use({}, inputs, {&write, 1});
    commands.BuildAccelerationStructure(structure);

    // The scratch memory goes away once the build is done.
    Impl* impl = m_impl.get();
    m_impl->Defer([impl, scratch = data.scratch, allocation = data.scratchAllocation] {
        vmaDestroyBuffer(impl->allocator, scratch, allocation);
    });
    data.scratch = VK_NULL_HANDLE;
    data.scratchAllocation = nullptr;
    data.scratchAddress = 0;
    return {};
}

void Device::WriteInstances(BufferHandle buffer, std::uint64_t offset,
                            std::span<const AccelerationStructureInstance> instances) {
    const std::span<std::byte> mapped = Mapped(buffer);
    DAI_ASSERT(offset + instances.size() * kAccelerationStructureInstanceSize <= mapped.size(),
               "The instances do not fit in the buffer");
    static_assert(sizeof(VkAccelerationStructureInstanceKHR) == kAccelerationStructureInstanceSize);
    for (std::size_t index = 0; index < instances.size(); ++index) {
        const AccelerationStructureInstance& instance = instances[index];
        VkAccelerationStructureInstanceKHR record{};
        // Vulkan wants the first three rows, row by row; GLM stores columns.
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column) {
                record.transform.matrix[row][column] = instance.transform[column][row];
            }
        }
        record.instanceCustomIndex = instance.customIndex & 0xFFFFFFU;
        record.mask = instance.mask;
        record.instanceShaderBindingTableRecordOffset = 0;
        VkGeometryInstanceFlagsKHR flags =
            instance.opaque ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
        if (instance.doubleSided) {
            flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        }
        record.flags = flags;
        record.accelerationStructureReference = m_impl->accelerationStructures.Get(instance.structure).address;
        std::memcpy(mapped.data() + offset + index * kAccelerationStructureInstanceSize, &record, sizeof(record));
    }
}

void CommandList::BuildAccelerationStructure(AccelerationStructureHandle structure) {
    BuildAccelerationStructures({&structure, 1});
}

void CommandList::BuildAccelerationStructure(AccelerationStructureHandle structure, std::uint32_t boxes) {
    Device::Impl& device = *m_recording->device;
    AccelerationStructureData& data = device.accelerationStructures.Get(structure);
    DAI_ASSERT(data.desc.level == AccelerationStructureLevel::Bottom && data.desc.boxes.count > 0 && !data.desc.refit,
               "Only a structure of boxes, rebuilt each time, builds part of them");
    DAI_ASSERT(data.scratch != VK_NULL_HANDLE, "This structure was built with the uploads and cannot be built again");
    DAI_ASSERT(CurrentAccess(data.access) == Access::AccelerationStructureWrite,
               "Declare the structure as AccelerationStructureWrite first");
    BuildGeometry geometry = BottomGeometry(device, data.desc);
    geometry.primitives = std::min(boxes, data.desc.boxes.count);
    data.built = true;
    RecordBuild(m_recording->commands, data, geometry);
}

void CommandList::BuildAccelerationStructures(std::span<const AccelerationStructureHandle> structures) {
    Device::Impl& device = *m_recording->device;
    std::vector<BuildGeometry> geometries;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    geometries.reserve(structures.size());
    for (const AccelerationStructureHandle structure : structures) {
        const AccelerationStructureData& data = device.accelerationStructures.Get(structure);
        DAI_ASSERT(data.desc.level == AccelerationStructureLevel::Bottom, "A top-level build needs its instances");
        DAI_ASSERT(data.scratch != VK_NULL_HANDLE,
                   "This structure was built with the uploads and cannot be built again");
        DAI_ASSERT(CurrentAccess(data.access) == Access::AccelerationStructureWrite,
                   "Declare the structure as AccelerationStructureWrite first");
        geometries.push_back(BottomGeometry(device, data.desc));
    }
    // The geometries are all stored before any info points to one.
    for (std::size_t index = 0; index < structures.size(); ++index) {
        AccelerationStructureData& data = device.accelerationStructures.Get(structures[index]);
        VkAccelerationStructureBuildGeometryInfoKHR info = BuildInfo(data.desc, geometries[index].geometry);
        if (data.desc.refit && data.built) {
            info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
            info.srcAccelerationStructure = data.structure;
        }
        data.built = true;
        info.dstAccelerationStructure = data.structure;
        info.scratchData.deviceAddress = data.scratchAddress;
        infos.push_back(info);
        ranges.push_back({.primitiveCount = geometries[index].primitives});
    }
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers;
    for (const VkAccelerationStructureBuildRangeInfoKHR& range : ranges) {
        rangePointers.push_back(&range);
    }
    if (!infos.empty()) {
        vkCmdBuildAccelerationStructuresKHR(m_recording->commands, static_cast<std::uint32_t>(infos.size()),
                                            infos.data(), rangePointers.data());
    }
}

void CommandList::BuildAccelerationStructure(AccelerationStructureHandle structure, BufferHandle instances,
                                             std::uint32_t count) {
    Device::Impl& device = *m_recording->device;
    const AccelerationStructureData& data = device.accelerationStructures.Get(structure);
    DAI_ASSERT(data.desc.level == AccelerationStructureLevel::Top, "A bottom-level build takes no instances");
    DAI_ASSERT(count <= data.desc.maxInstances, "More instances than the structure was created for");
    DAI_ASSERT(CurrentAccess(data.access) == Access::AccelerationStructureWrite,
               "Declare the structure as AccelerationStructureWrite first");
    RecordBuild(m_recording->commands, data, Instances(device, instances, count));
}

} // namespace daidalos::rhi
