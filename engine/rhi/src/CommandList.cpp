// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/rhi/CommandList.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

struct StateAccess {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

VkImageLayout LayoutOf(Access access) {
    if (HasFlag(access, Access::ColorTarget)) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (HasFlag(access, Access::DepthTarget)) {
        return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    if (HasFlag(access, Access::ComputeShaderWrite)) {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    if (HasFlag(access, Access::VertexShaderRead | Access::FragmentShaderRead | Access::ComputeShaderRead)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (HasFlag(access, Access::TransferRead)) {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (HasFlag(access, Access::TransferWrite)) {
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    if (HasFlag(access, Access::Present)) {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

// The stages and memory accesses to wait for, or to make wait, for an access.
StateAccess Describe(Access access, bool texture) {
    if (access == Access::None) {
        // A buffer's earlier uses are unknown: wait for everything. A texture without a use is new or
        // a swapchain image, which becomes usable when the acquire semaphore's wait at the color output
        // stage ends: its first barrier must start from that stage to run after the wait.
        return texture ? StateAccess{.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}
                       : StateAccess{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
    StateAccess result;
    const auto add = [&](Access flag, VkPipelineStageFlags2 stages, VkAccessFlags2 accesses) {
        if (HasFlag(access, flag)) {
            result.stages |= stages;
            result.access |= accesses;
        }
    };
    add(Access::ColorTarget, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    add(Access::DepthTarget, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    add(Access::VertexShaderRead, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    add(Access::FragmentShaderRead, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    add(Access::ComputeShaderRead, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    add(Access::ComputeShaderWrite, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    add(Access::AccelerationStructureWrite, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    add(Access::AccelerationStructureRead, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    add(Access::RayQueryRead, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    add(Access::VertexInputRead, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    add(Access::VertexInputRead, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    add(Access::IndirectRead, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    add(Access::TransferRead, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    add(Access::TransferWrite, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // Presentation waits on a semaphore: the barrier only has to finish before the color output ends.
    add(Access::Present, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE);
    return result;
}

VkImageAspectFlags AspectOf(Format format) {
    return (format == Format::D32Float) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkBufferImageCopy WholeLevel(const TextureData& texture, std::uint32_t level, std::uint64_t bufferOffset) {
    const Extent extent = MipExtent(texture.extent, level);
    return {.bufferOffset = bufferOffset,
            .imageSubresource = {.aspectMask = AspectOf(texture.format), .mipLevel = level, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1}};
}

} // namespace

Access AccessOf(TextureState state) {
    switch (state) {
    case TextureState::Undefined:
        return Access::None;
    case TextureState::ColorTarget:
        return Access::ColorTarget;
    case TextureState::DepthTarget:
        return Access::DepthTarget;
    case TextureState::ShaderRead:
        return Access::VertexShaderRead | Access::FragmentShaderRead | Access::ComputeShaderRead;
    case TextureState::TransferSrc:
        return Access::TransferRead;
    case TextureState::TransferDst:
        return Access::TransferWrite;
    case TextureState::Present:
        return Access::Present;
    }
    return Access::None;
}

AccessStep NextAccess(AccessState& state, Access next, bool texture, bool discard) {
    const Access earlier = state.lastWrite | state.reads;
    const bool layoutChange = texture && (discard || LayoutOf(CurrentAccess(state)) != LayoutOf(next));
    if (Writes(next) || layoutChange) {
        // A texture that was never used needs a barrier only to leave the undefined layout.
        const bool barrier = earlier != Access::None || !texture || LayoutOf(next) != VK_IMAGE_LAYOUT_UNDEFINED;
        if (Writes(next)) {
            state = {.lastWrite = next, .reads = Access::None};
        } else {
            state.reads = next;
        }
        return {.barrier = barrier, .source = earlier};
    }
    // A read in stages that an earlier barrier already made the last write visible to.
    const auto covered = static_cast<std::uint32_t>(state.reads);
    const auto wanted = static_cast<std::uint32_t>(next);
    if ((wanted & ~covered) == 0 && (texture || earlier != Access::None)) {
        return {};
    }
    state.reads = state.reads | next;
    return {.barrier = true, .source = earlier};
}

void CommandList::Use(std::span<const TextureUse> textures, std::span<const BufferUse> buffers,
                      std::span<const AccelerationStructureUse> structures) {
    Device::Impl& device = *m_recording->device;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    for (const TextureUse& use : textures) {
        TextureData& data = device.textures.Get(use.texture);
        const Access before = CurrentAccess(data.access);
        const AccessStep step = NextAccess(data.access, use.access, true, use.discard);
        if (step.barrier) {
            const StateAccess source = Describe(step.source, true);
            const StateAccess destination = Describe(use.access, true);
            imageBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = source.stages,
                .srcAccessMask = source.access,
                .dstStageMask = destination.stages,
                .dstAccessMask = destination.access,
                .oldLayout = use.discard ? VK_IMAGE_LAYOUT_UNDEFINED : LayoutOf(before),
                .newLayout = LayoutOf(CurrentAccess(data.access)),
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = data.image,
                .subresourceRange = {.aspectMask = AspectOf(data.format),
                                     .levelCount = data.mipLevels,
                                     .layerCount = data.layers},
            });
        }
    }
    for (const BufferUse& use : buffers) {
        BufferData& data = device.buffers.Get(use.buffer);
        const AccessStep step = NextAccess(data.access, use.access, false);
        if (step.barrier) {
            const StateAccess source = Describe(step.source, false);
            const StateAccess destination = Describe(use.access, false);
            bufferBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = source.stages,
                .srcAccessMask = source.access,
                .dstStageMask = destination.stages,
                .dstAccessMask = destination.access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = data.buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            });
        }
    }
    std::vector<VkMemoryBarrier2> memoryBarriers;
    for (const AccelerationStructureUse& use : structures) {
        AccelerationStructureData& data = device.accelerationStructures.Get(use.structure);
        const AccessStep step = NextAccess(data.access, use.access, false);
        if (step.barrier) {
            const StateAccess source = Describe(step.source, false);
            const StateAccess destination = Describe(use.access, false);
            memoryBarriers.push_back({.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                      .srcStageMask = source.stages,
                                      .srcAccessMask = source.access,
                                      .dstStageMask = destination.stages,
                                      .dstAccessMask = destination.access});
        }
    }
    if (imageBarriers.empty() && bufferBarriers.empty() && memoryBarriers.empty()) {
        return;
    }
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = static_cast<std::uint32_t>(memoryBarriers.size()),
        .pMemoryBarriers = memoryBarriers.data(),
        .bufferMemoryBarrierCount = static_cast<std::uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data(),
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(imageBarriers.size()),
        .pImageMemoryBarriers = imageBarriers.data(),
    };
    vkCmdPipelineBarrier2(m_recording->commands, &dependency);
}

void CommandList::Transition(TextureHandle texture, TextureState state) {
    const TextureUse use{texture, AccessOf(state)};
    Use({&use, 1}, {});
}

void CommandList::Discard(TextureHandle texture, TextureState state) {
    const TextureUse use{texture, AccessOf(state), true};
    Use({&use, 1}, {});
}

void CommandList::BeginLabel(std::string_view name) {
    if (!m_recording->device->debugUtils) {
        return;
    }
    const std::string text(name);
    const VkDebugUtilsLabelEXT label{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = text.c_str()};
    vkCmdBeginDebugUtilsLabelEXT(m_recording->commands, &label);
}

void CommandList::EndLabel() {
    if (m_recording->device->debugUtils) {
        vkCmdEndDebugUtilsLabelEXT(m_recording->commands);
    }
}

void CommandList::BeginRendering(const RenderingDesc& desc) {
    Device::Impl& device = *m_recording->device;
    DAI_ASSERT(desc.colorTarget || desc.depthTarget, "Rendering needs a color target, a depth target or both");
    DAI_ASSERT(desc.colorTarget || desc.moreColorTargets.empty(), "Further color targets need a first one");
    DAI_ASSERT(desc.moreColorTargets.size() < kMaxColorTargets, "Too many color targets");
    Extent extent;
    std::array<VkRenderingAttachmentInfo, kMaxColorTargets> colorAttachments{};
    std::uint32_t colorCount = 0;
    const auto addColor = [&](TextureHandle texture, const std::optional<glm::vec4>& clear) {
        const TextureData& color = device.textures.Get(texture);
        DAI_ASSERT(CurrentAccess(color.access) == Access::ColorTarget,
                   "The color targets must be in the ColorTarget state");
        DAI_ASSERT(colorCount == 0 || color.extent == extent, "The color targets must have the same size");
        extent = color.extent;
        VkRenderingAttachmentInfo& attachment = colorAttachments[colorCount++];
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = color.view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = clear.has_value() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clear.has_value()) {
            attachment.clearValue.color = {{clear->r, clear->g, clear->b, clear->a}};
        }
    };
    if (desc.colorTarget) {
        addColor(desc.colorTarget, desc.clearColor);
        for (const ColorTarget& target : desc.moreColorTargets) {
            addColor(target.texture, target.clear);
        }
    }

    VkRenderingAttachmentInfo depthAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (desc.depthTarget) {
        const TextureData& depth = device.textures.Get(desc.depthTarget);
        DAI_ASSERT(CurrentAccess(depth.access) == Access::DepthTarget,
                   "The depth target must be in the DepthTarget state");
        DAI_ASSERT(!desc.colorTarget || depth.extent == extent, "The color and depth targets must have the same size");
        extent = depth.extent;
        depthAttachment.imageView = depth.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = desc.clearDepth.has_value() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {desc.clearDepth.value_or(0.0f), 0};
    }

    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {extent.width, extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = colorCount,
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = desc.depthTarget ? &depthAttachment : nullptr,
    };
    vkCmdBeginRendering(m_recording->commands, &renderingInfo);

    SetViewport(0, 0, extent.width, extent.height);
}

void CommandList::SetViewport(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) {
    // The one place where clip-space Y is flipped (CONVENTIONS.md, section 5).
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    const auto fullHeight = static_cast<float>(height);
    const VkViewport viewport{left, top + fullHeight, static_cast<float>(width), -fullHeight, 0.0f, 1.0f};
    vkCmdSetViewport(m_recording->commands, 0, 1, &viewport);
    const VkRect2D scissor{{x, y}, {width, height}};
    vkCmdSetScissor(m_recording->commands, 0, 1, &scissor);
}

void CommandList::EndRendering() {
    vkCmdEndRendering(m_recording->commands);
}

void CommandList::BindPipeline(PipelineHandle pipeline) {
    Device::Impl& device = *m_recording->device;
    const PipelineData& data = device.pipelines.Get(pipeline);
    vkCmdBindPipeline(m_recording->commands, data.bindPoint, data.pipeline);
    vkCmdBindDescriptorSets(m_recording->commands, data.bindPoint, device.pipelineLayout, 0, 1, &device.globalSet, 0,
                            nullptr);
}

void CommandList::BindVertexBuffer(BufferHandle buffer, std::uint64_t offset) {
    const VkBuffer vertexBuffer = m_recording->device->buffers.Get(buffer).buffer;
    vkCmdBindVertexBuffers(m_recording->commands, 0, 1, &vertexBuffer, &offset);
}

void CommandList::BindIndexBuffer(BufferHandle buffer, IndexType type, std::uint64_t offset) {
    vkCmdBindIndexBuffer(m_recording->commands, m_recording->device->buffers.Get(buffer).buffer, offset,
                         (type == IndexType::Uint16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void CommandList::SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) {
    const VkRect2D scissor{{x, y}, {width, height}};
    vkCmdSetScissor(m_recording->commands, 0, 1, &scissor);
}

void CommandList::PushConstants(std::span<const std::byte> data) {
    DAI_ASSERT(data.size() <= kMaxPushConstantSize, "Push constants are limited to kMaxPushConstantSize bytes");
    vkCmdPushConstants(m_recording->commands, m_recording->device->pipelineLayout, VK_SHADER_STAGE_ALL, 0,
                       static_cast<std::uint32_t>(data.size()), data.data());
}

bool CommandList::WriteTimestamp() {
    detail::CommandRecording& recording = *m_recording;
    if (!recording.firstTimestamp.has_value() || recording.timestampCount >= kMaxTimestamps) {
        return false;
    }
    vkCmdWriteTimestamp2(recording.commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, recording.device->timestamps,
                         *recording.firstTimestamp + recording.timestampCount);
    ++recording.timestampCount;
    return true;
}

void CommandList::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    vkCmdDraw(m_recording->commands, vertexCount, 1, firstVertex, 0);
}

void CommandList::DrawIndirect(BufferHandle arguments, std::uint64_t offset) {
    vkCmdDrawIndirect(m_recording->commands, m_recording->device->buffers.Get(arguments).buffer, offset, 1,
                      sizeof(VkDrawIndirectCommand));
}

void CommandList::Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) {
    vkCmdDispatch(m_recording->commands, groupsX, groupsY, groupsZ);
}

void CommandList::Barrier() {
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(m_recording->commands, &dependency);
}

void CommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) {
    vkCmdDrawIndexed(m_recording->commands, indexCount, 1, firstIndex, vertexOffset, 0);
}

void CommandList::CopyBuffer(BufferHandle source, BufferHandle destination, std::uint64_t size,
                             std::uint64_t sourceOffset, std::uint64_t destinationOffset) {
    Device::Impl& device = *m_recording->device;
    const VkBufferCopy region{sourceOffset, destinationOffset, size};
    vkCmdCopyBuffer(m_recording->commands, device.buffers.Get(source).buffer, device.buffers.Get(destination).buffer, 1,
                    &region);
}

void CommandList::CopyBufferToTexture(BufferHandle source, TextureHandle destination) {
    Device::Impl& device = *m_recording->device;
    const TextureData& texture = device.textures.Get(destination);
    DAI_ASSERT(CurrentAccess(texture.access) == Access::TransferWrite, "The texture must be in the TransferDst state");
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(std::size_t{texture.mipLevels} * texture.layers);
    std::uint64_t offset = 0;
    for (std::uint32_t layer = 0; layer < texture.layers; ++layer) {
        for (std::uint32_t level = 0; level < texture.mipLevels; ++level) {
            VkBufferImageCopy region = WholeLevel(texture, level, offset);
            region.imageSubresource.baseArrayLayer = layer;
            regions.push_back(region);
            offset += MipByteSize(texture.format, MipExtent(texture.extent, level));
        }
    }
    vkCmdCopyBufferToImage(m_recording->commands, device.buffers.Get(source).buffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()),
                           regions.data());
}

void CommandList::CopyTextureToBuffer(TextureHandle source, BufferHandle destination, std::uint32_t mipLevel) {
    Device::Impl& device = *m_recording->device;
    const TextureData& texture = device.textures.Get(source);
    DAI_ASSERT(CurrentAccess(texture.access) == Access::TransferRead, "The texture must be in the TransferSrc state");
    DAI_ASSERT(mipLevel < texture.mipLevels, "The texture does not have this mip level");
    const VkBufferImageCopy region = WholeLevel(texture, mipLevel, 0);
    vkCmdCopyImageToBuffer(m_recording->commands, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           device.buffers.Get(destination).buffer, 1, &region);
}

} // namespace daidalos::rhi
