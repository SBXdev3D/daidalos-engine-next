// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Handle.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace daidalos::rhi {

// Handles stay usable as values anywhere; only the Device that created them can resolve them.
struct BufferTag;
struct TextureTag;
struct PipelineTag;
using BufferHandle = core::Handle<BufferTag>;
using TextureHandle = core::Handle<TextureTag>;
using PipelineHandle = core::Handle<PipelineTag>;

/// Frames that the CPU can record ahead of the GPU.
inline constexpr std::uint32_t kFramesInFlight = 2;
/// Timestamps that one frame can write.
inline constexpr std::uint32_t kMaxTimestamps = 32;

/// Size of the push constant block. Vulkan guarantees 128 bytes on every device.
inline constexpr std::uint32_t kMaxPushConstantSize = 128;

struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent&) const = default;
};

enum class Format {
    Undefined,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
    Rg32Float,
    Rgb32Float,
    Rgba32Float,
    /// High dynamic range color, for lighting before tonemapping.
    Rgba16Float,
    /// One channel, such as ambient occlusion.
    R8Unorm,
    D32Float,
    /// Block-compressed formats, with blocks of 4x4 pixels.
    Bc5Unorm,
    Bc7Unorm,
    Bc7Srgb,
};

/// Bytes per pixel. Undefined and block-compressed formats have none: use MipByteSize.
[[nodiscard]] std::uint32_t BytesPerPixel(Format format);
/// Size of a mip level: half of the previous one, rounded down, and never below 1.
[[nodiscard]] Extent MipExtent(Extent extent, std::uint32_t level);
/// Tightly packed size of one mip level.
[[nodiscard]] std::uint64_t MipByteSize(Format format, Extent extent);

enum class BufferUsage : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    TransferSrc = 1U << 2U,
    TransferDst = 1U << 3U,
    /// Readable from shaders through the storage buffer arrays, with Device::StorageIndex.
    Storage = 1U << 4U,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
[[nodiscard]] constexpr bool HasFlag(BufferUsage flags, BufferUsage flag) {
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1U << 0U,
    ColorTarget = 1U << 1U,
    DepthTarget = 1U << 2U,
    TransferSrc = 1U << 3U,
    TransferDst = 1U << 4U,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
[[nodiscard]] constexpr bool HasFlag(TextureUsage flags, TextureUsage flag) {
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class MemoryLocation {
    /// Fastest for the GPU; filled with Device::UploadBuffer.
    GpuOnly,
    /// Written by the CPU through Device::Mapped, read by the GPU.
    CpuToGpu,
    /// Written by the GPU, read by the CPU through Device::Mapped.
    GpuToCpu,
};

struct BufferDesc {
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryLocation memory = MemoryLocation::GpuOnly;
    /// Shown by validation messages and debuggers.
    std::string name;
};

struct TextureDesc {
    Extent extent;
    Format format = Format::Undefined;
    TextureUsage usage = TextureUsage::None;
    std::uint32_t mipLevels = 1;
    std::string name;
};

/// What a texture is used for next. A CommandList::Transition between two states is the only
/// synchronization that the RHI asks of its users.
enum class TextureState {
    Undefined,
    ColorTarget,
    DepthTarget,
    ShaderRead,
    TransferSrc,
    TransferDst,
    Present,
};

enum class IndexType { Uint16, Uint32 };
enum class CullMode { None, Front, Back };
enum class BlendMode { Opaque, Alpha };

/// Indices into the sampler array of the global descriptor set, declared as
/// [[vk::binding(1, 0)]] SamplerState g_samplers[4] in shaders. ShadowCompare is a comparison sampler,
/// declared as SamplerComparisonState on the same binding: it passes where the reference depth is
/// nearer than or as near as the stored one (reverse-Z), and filters the four results bilinearly.
enum class SamplerIndex : std::uint32_t { LinearClamp, LinearRepeat, NearestClamp, ShadowCompare };
inline constexpr std::uint32_t kSamplerCount = 4;

struct VertexAttribute {
    std::uint32_t location = 0;
    Format format = Format::Undefined;
    std::uint32_t offset = 0;
};

/// A pipeline uses the push constant block and the global descriptor set: every sampled texture at
/// [[vk::binding(0, 0)]] Texture2D g_textures[], indexed with Device::BindlessIndex, and every storage
/// buffer at [[vk::binding(2, 0)]] StructuredBuffer<T> g_name[], indexed with Device::StorageIndex.
/// Shader entry points are named VSMain and PSMain.
struct GraphicsPipelineDesc {
    std::span<const std::uint32_t> vertexShader;
    std::span<const std::uint32_t> fragmentShader;
    std::uint32_t vertexStride = 0;
    std::vector<VertexAttribute> vertexAttributes;
    /// Undefined for a pipeline that only writes depth, such as a shadow map's.
    Format colorFormat = Format::Undefined;
    /// Undefined when the pipeline renders without a depth target.
    Format depthFormat = Format::Undefined;
    /// Added to the depth written, in units of the smallest depth step, and per unit of slope. Reverse-Z:
    /// negative values push surfaces away from the viewer.
    float depthBias = 0.0f;
    float depthBiasSlope = 0.0f;
    /// Reverse-Z comparison (CONVENTIONS.md, section 5). Ignored without a depth target.
    bool depthTest = true;
    bool depthWrite = true;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    std::string name;
};

} // namespace daidalos::rhi
