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
#include <type_traits>

namespace daidalos::rhi {

namespace detail {
struct CommandRecording;
} // namespace detail

struct RenderingDesc {
    TextureHandle colorTarget;
    /// Clears the color target. Without it, the previous content is kept.
    std::optional<glm::vec4> clearColor;
    /// Null to render without depth.
    TextureHandle depthTarget;
    /// Clears the depth target. Reverse-Z clears to 0, the far plane.
    std::optional<float> clearDepth;
};

/// Records GPU commands. It is a small value that is valid only during the Device::Submit callback
/// or between Device::BeginFrame and Device::EndFrame that produced it.
class CommandList {
public:
    explicit CommandList(detail::CommandRecording& recording) : m_recording(&recording) {}

    /// Declares the next use of a texture. The Device tracks the current state.
    void Transition(TextureHandle texture, TextureState state);
    /// Same as Transition for a texture about to be overwritten: its content is dropped. It orders
    /// the new use after the previous one even when the state does not change, which a target
    /// reused by every frame needs.
    void Discard(TextureHandle texture, TextureState state);

    /// Sets the viewport (flipped once to make clip-space Y point up, CONVENTIONS.md section 5) and
    /// the scissor to the size of the color target.
    void BeginRendering(const RenderingDesc& desc);
    void EndRendering();

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

    void CopyBuffer(BufferHandle source, BufferHandle destination, std::uint64_t size, std::uint64_t sourceOffset = 0,
                    std::uint64_t destinationOffset = 0);
    /// Fills every mip level, laid out as for Device::UploadTexture. The texture must be in the
    /// TransferDst state.
    void CopyBufferToTexture(BufferHandle source, TextureHandle destination);
    /// The texture must be in the TransferSrc state. Pixels are tightly packed, row by row from the top.
    void CopyTextureToBuffer(TextureHandle source, BufferHandle destination, std::uint32_t mipLevel = 0);

private:
    detail::CommandRecording* m_recording;
};

} // namespace daidalos::rhi
