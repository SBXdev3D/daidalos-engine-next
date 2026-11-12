// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/rhi/Types.hpp>

#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace daidalos::rhi {

namespace detail {
struct CommandRecording;
} // namespace detail

/// A color target after the first one of a rendering.
struct ColorTarget {
    TextureHandle texture;
    /// Clears it. Without it, the previous content is kept.
    std::optional<glm::vec4> clear;
};

struct RenderingDesc {
    /// Null to render depth only.
    TextureHandle colorTarget;
    /// Clears the color target. Without it, the previous content is kept.
    std::optional<glm::vec4> clearColor;
    /// Further color targets, the fragment shader's outputs 1 and up, of the same size as the first. At
    /// most kMaxColorTargets in all.
    std::vector<ColorTarget> moreColorTargets;
    /// Null to render without depth.
    TextureHandle depthTarget;
    /// Clears the depth target. Reverse-Z clears to 0, the far plane.
    std::optional<float> clearDepth;
};

/// The next use of a texture. Discard drops its content: for a texture about to be overwritten.
struct TextureUse {
    TextureHandle texture;
    Access access = Access::None;
    bool discard = false;
};

/// The next use of a buffer.
struct BufferUse {
    BufferHandle buffer;
    Access access = Access::None;
};

/// The next use of an acceleration structure.
struct AccelerationStructureUse {
    AccelerationStructureHandle structure;
    Access access = Access::None;
};

/// Records GPU commands. It is a small value that is valid only during the Device::Submit callback
/// or between Device::BeginFrame and Device::EndFrame that produced it.
class CommandList {
public:
    explicit CommandList(detail::CommandRecording& recording) : m_recording(&recording) {}

    /// Declares the next uses of textures and buffers. The Device tracks what each was last used for,
    /// and records one barrier for the uses that must wait (NextAccess). The render graph declares every
    /// use of its passes this way (ADR-0032).
    void Use(std::span<const TextureUse> textures, std::span<const BufferUse> buffers,
             std::span<const AccelerationStructureUse> structures = {});
    /// Declares the next use of a texture: Use with the access of a state.
    void Transition(TextureHandle texture, TextureState state);
    /// Same as Transition for a texture about to be overwritten: its content is dropped. It orders
    /// the new use after the previous one even when the state does not change, which a target
    /// reused by every frame needs.
    void Discard(TextureHandle texture, TextureState state);

    /// Sets the viewport (flipped once to make clip-space Y point up, CONVENTIONS.md section 5) and
    /// the scissor to the size of the targets.
    void BeginRendering(const RenderingDesc& desc);
    /// Draws into a part of the targets only, in pixels from their top-left corner. Clip-space Y still
    /// points up. Also sets the scissor to that part.
    void SetViewport(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height);
    void EndRendering();

    /// Binds a graphics or a compute pipeline.
    void BindPipeline(PipelineHandle pipeline);
    void BindVertexBuffer(BufferHandle buffer, std::uint64_t offset = 0);
    void BindIndexBuffer(BufferHandle buffer, IndexType type, std::uint64_t offset = 0);
    void SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height);
    void PushConstants(std::span<const std::byte> data);

    /// Pushes a trivially copyable block of at most kMaxPushConstantSize bytes.
    template <class T>
        requires std::is_trivially_copyable_v<T>
    void Push(const T& data) {
        static_assert(sizeof(T) <= kMaxPushConstantSize);
        PushConstants(std::as_bytes(std::span(&data, 1)));
    }

    /// Records the GPU clock once every earlier command of the frame has finished. Returns false, and
    /// records nothing, outside a frame, past kMaxTimestamps, or on a device without timestamps.
    /// Device::CompletedTimestamps reads them once the frame is done.
    bool WriteTimestamp();

    void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0);
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0);
    /// Draws with the arguments that the GPU wrote at offset in a BufferUsage::Indirect buffer: vertex count,
    /// instance count, first vertex and first instance, as 32-bit integers.
    void DrawIndirect(BufferHandle arguments, std::uint64_t offset = 0);

    /// Runs the bound compute pipeline on groups of threads. Outside any rendering scope.
    void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY = 1, std::uint32_t groupsZ = 1);
    /// Everything recorded before writes its buffers before anything recorded after reads or writes them,
    /// indirect arguments included. Textures keep their own transitions. Coarse: Use is precise.
    void Barrier();

    /// Names the commands up to the matching EndLabel, for graphics debuggers. Labels nest. Nothing is
    /// recorded when the debug utilities are unavailable.
    void BeginLabel(std::string_view name);
    /// Builds a bottom-level structure from the geometry it was created with, outside any rendering scope.
    /// Declare the structure as AccelerationStructureWrite and its buffers as AccelerationStructureRead.
    void BuildAccelerationStructure(AccelerationStructureHandle structure);
    /// Builds several bottom-level structures in one command, which the GPU can run side by side.
    void BuildAccelerationStructures(std::span<const AccelerationStructureHandle> structures);
    /// Builds a top-level structure from count instances written in a buffer (Device::WriteInstances).
    /// Declare the bottom-level structures it holds as AccelerationStructureRead too.
    void BuildAccelerationStructure(AccelerationStructureHandle structure, BufferHandle instances, std::uint32_t count);
    void EndLabel();

    void CopyBuffer(BufferHandle source, BufferHandle destination, std::uint64_t size, std::uint64_t sourceOffset = 0,
                    std::uint64_t destinationOffset = 0);
    /// Fills every mip level, laid out as for Device::UploadTexture. The texture must be in the
    /// TransferDst state.
    void CopyBufferToTexture(BufferHandle source, TextureHandle destination);
    /// Copies the first mip level of every layer to a texture of the same size, format and layers. The source
    /// must be in the TransferSrc state and the destination in the TransferDst state.
    void CopyTexture(TextureHandle source, TextureHandle destination);
    /// The texture must be in the TransferSrc state. Pixels are tightly packed, row by row from the top.
    void CopyTextureToBuffer(TextureHandle source, BufferHandle destination, std::uint32_t mipLevel = 0);

private:
    detail::CommandRecording* m_recording;
};

} // namespace daidalos::rhi
