// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/rhi/CommandList.hpp>

#include "DeviceImpl.hpp"

namespace daidalos::rhi {

namespace {

struct StateAccess {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

StateAccess Describe(TextureState state) {
    switch (state) {
    // A swapchain image becomes usable when the acquire semaphore's wait at the color output stage
    // ends. Its first barrier must start from that stage to run after the wait, so Undefined and
    // Present name it rather than no stage at all.
    case TextureState::Undefined:
        return {.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    case TextureState::ColorTarget:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case TextureState::DepthTarget:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
    case TextureState::ShaderRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case TextureState::TransferSrc:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case TextureState::TransferDst:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case TextureState::Present:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    return {};
}

VkImageAspectFlags AspectOf(Format format) {
    return (format == Format::D32Float) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkBufferImageCopy WholeImage(const TextureData& texture) {
    return {.imageSubresource = {.aspectMask = AspectOf(texture.format), .layerCount = 1},
            .imageExtent = {texture.extent.width, texture.extent.height, 1}};
}

// Waits for the use described by the texture's current state, then moves it to the new state.
void Barrier(detail::CommandRecording& recording, TextureData& data, VkImageLayout oldLayout, TextureState state) {
    const StateAccess source = Describe(data.state);
    const StateAccess destination = Describe(state);
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = source.stages,
        .srcAccessMask = source.access,
        .dstStageMask = destination.stages,
        .dstAccessMask = destination.access,
        .oldLayout = oldLayout,
        .newLayout = destination.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = data.image,
        .subresourceRange = {.aspectMask = AspectOf(data.format), .levelCount = 1, .layerCount = 1},
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(recording.commands, &dependency);
    data.state = state;
}

} // namespace

void CommandList::Transition(TextureHandle texture, TextureState state) {
    TextureData& data = m_recording->device->textures.Get(texture);
    if (data.state == state) {
        return;
    }
    Barrier(*m_recording, data, Describe(data.state).layout, state);
}

void CommandList::Discard(TextureHandle texture, TextureState state) {
    TextureData& data = m_recording->device->textures.Get(texture);
    Barrier(*m_recording, data, VK_IMAGE_LAYOUT_UNDEFINED, state);
}

void CommandList::BeginRendering(const RenderingDesc& desc) {
    Device::Impl& device = *m_recording->device;
    const TextureData& color = device.textures.Get(desc.colorTarget);
    DAI_ASSERT(color.state == TextureState::ColorTarget, "The color target must be in the ColorTarget state");

    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = desc.clearColor.has_value() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    if (desc.clearColor.has_value()) {
        const glm::vec4& clear = *desc.clearColor;
        colorAttachment.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};
    }

    VkRenderingAttachmentInfo depthAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (desc.depthTarget) {
        const TextureData& depth = device.textures.Get(desc.depthTarget);
        DAI_ASSERT(depth.state == TextureState::DepthTarget, "The depth target must be in the DepthTarget state");
        DAI_ASSERT(depth.extent == color.extent, "The color and depth targets must have the same size");
        depthAttachment.imageView = depth.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = desc.clearDepth.has_value() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {desc.clearDepth.value_or(0.0f), 0};
    }

    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {color.extent.width, color.extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = desc.depthTarget ? &depthAttachment : nullptr,
    };
    vkCmdBeginRendering(m_recording->commands, &renderingInfo);

    // The one place where clip-space Y is flipped (CONVENTIONS.md, section 5).
    const auto width = static_cast<float>(color.extent.width);
    const auto height = static_cast<float>(color.extent.height);
    const VkViewport viewport{0.0f, height, width, -height, 0.0f, 1.0f};
    vkCmdSetViewport(m_recording->commands, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, {color.extent.width, color.extent.height}};
    vkCmdSetScissor(m_recording->commands, 0, 1, &scissor);
}

void CommandList::EndRendering() {
    vkCmdEndRendering(m_recording->commands);
}

void CommandList::BindPipeline(PipelineHandle pipeline) {
    Device::Impl& device = *m_recording->device;
    vkCmdBindPipeline(m_recording->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, device.pipelines.Get(pipeline).pipeline);
    vkCmdBindDescriptorSets(m_recording->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, device.pipelineLayout, 0, 1,
                            &device.globalSet, 0, nullptr);
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
    vkCmdPushConstants(m_recording->commands, m_recording->device->pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                       static_cast<std::uint32_t>(data.size()), data.data());
}

void CommandList::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    vkCmdDraw(m_recording->commands, vertexCount, 1, firstVertex, 0);
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
    DAI_ASSERT(texture.state == TextureState::TransferDst, "The texture must be in the TransferDst state");
    const VkBufferImageCopy region = WholeImage(texture);
    vkCmdCopyBufferToImage(m_recording->commands, device.buffers.Get(source).buffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void CommandList::CopyTextureToBuffer(TextureHandle source, BufferHandle destination) {
    Device::Impl& device = *m_recording->device;
    const TextureData& texture = device.textures.Get(source);
    DAI_ASSERT(texture.state == TextureState::TransferSrc, "The texture must be in the TransferSrc state");
    const VkBufferImageCopy region = WholeImage(texture);
    vkCmdCopyImageToBuffer(m_recording->commands, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           device.buffers.Get(destination).buffer, 1, &region);
}

} // namespace daidalos::rhi
