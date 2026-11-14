// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

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
struct AccelerationStructureTag;
using BufferHandle = core::Handle<BufferTag>;
using TextureHandle = core::Handle<TextureTag>;
using PipelineHandle = core::Handle<PipelineTag>;
using AccelerationStructureHandle = core::Handle<AccelerationStructureTag>;

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
    /// Two half floats, such as motion vectors.
    Rg16Float,
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
    /// Readable from shaders through the storage buffer arrays, with Device::StorageIndex, and writable
    /// from compute shaders.
    Storage = 1U << 4U,
    /// Holds the arguments of CommandList::DrawIndirect.
    Indirect = 1U << 5U,
    /// Read by acceleration structure builds: vertices, indices or instances.
    AccelerationStructureInput = 1U << 6U,
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
    /// Read and written by compute shaders through the storage image array, with Device::StorageImageIndex.
    /// Rgba32Float or Rgba16Float only: shaders declare the format of the array they access it through.
    Storage = 1U << 5U,
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
    /// Six square faces, sampled by direction: +X, -X, +Y, -Y, +Z, -Z, in that order, each with its mip
    /// levels. Shaders find a sampled cube in g_cubeTextures, by BindlessIndex.
    bool cube = false;
    std::string name;
};

/// How the GPU uses a texture or a buffer, from which the RHI derives barriers (CommandList::Use,
/// ADR-0032). Reads combine; ColorTarget, DepthTarget, ComputeShaderWrite, TransferWrite and
/// AccelerationStructureWrite write.
enum class Access : std::uint32_t {
    None = 0,
    /// Rendered to, blending included.
    ColorTarget = 1U << 0U,
    /// Depth tested and written.
    DepthTarget = 1U << 1U,
    VertexShaderRead = 1U << 2U,
    FragmentShaderRead = 1U << 3U,
    ComputeShaderRead = 1U << 4U,
    /// Read and written by compute shaders.
    ComputeShaderWrite = 1U << 5U,
    /// Draw arguments.
    IndirectRead = 1U << 6U,
    TransferRead = 1U << 7U,
    TransferWrite = 1U << 8U,
    Present = 1U << 9U,
    /// An acceleration structure being built, and its scratch memory.
    AccelerationStructureWrite = 1U << 11U,
    /// Read by an acceleration structure build: its input buffers, or the bottom-level structures of a
    /// top-level build.
    AccelerationStructureRead = 1U << 12U,
    /// An acceleration structure traced by ray queries, in fragment or compute shaders.
    RayQueryRead = 1U << 13U,
    /// Vertices read by the input assembler.
    VertexInputRead = 1U << 14U,
};

[[nodiscard]] constexpr Access operator|(Access left, Access right) {
    return static_cast<Access>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
[[nodiscard]] constexpr bool HasFlag(Access flags, Access flag) {
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}
/// True if the access writes.
[[nodiscard]] constexpr bool Writes(Access access) {
    return HasFlag(access, Access::ColorTarget | Access::DepthTarget | Access::ComputeShaderWrite |
                               Access::TransferWrite | Access::AccelerationStructureWrite);
}

/// What the GPU last did with a resource: the last write, and the reads since then, which the write
/// was made visible to. A resource never used is None and None.
struct AccessState {
    Access lastWrite = Access::None;
    Access reads = Access::None;

    bool operator==(const AccessState&) const = default;
};

/// The access that decides a texture's layout: its reads, or its last write if nothing read it since.
[[nodiscard]] constexpr Access CurrentAccess(const AccessState& state) {
    return state.reads != Access::None ? state.reads : state.lastWrite;
}

/// Whether a use must wait, and for what.
struct AccessStep {
    bool barrier = false;
    /// What the barrier waits for: the last write and the reads since.
    Access source = Access::None;
};

/// Updates the state of a resource for its next use, and says whether a barrier must come first.
/// A write, a change of texture layout, or discarding a texture's content waits for every earlier
/// use. A read waits for the last write unless an earlier barrier already covered its stages: reads
/// in the same stages follow each other freely.
[[nodiscard]] AccessStep NextAccess(AccessState& state, Access next, bool texture, bool discard = false);

/// What a texture is used for next: a shorthand for an Access, used by CommandList::Transition.
enum class TextureState {
    Undefined,
    ColorTarget,
    DepthTarget,
    ShaderRead,
    TransferSrc,
    TransferDst,
    Present,
};

/// The access of a texture in a state. ShaderRead is a read by any shader stage.
[[nodiscard]] Access AccessOf(TextureState state);

/// Color targets a rendering and a pipeline can have.
inline constexpr std::uint32_t kMaxColorTargets = 4;

enum class IndexType { Uint16, Uint32 };
enum class CullMode { None, Front, Back };
/// Alpha blends color times alpha over the target. Premultiplied adds the color as given and keeps
/// (1 - alpha) of the target: an alpha of 0 adds light, as sparks do.
enum class BlendMode { Opaque, Alpha, Premultiplied };

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

/// A compute shader with the entry point CSMain, using the same push constants and global descriptor set
/// as graphics pipelines. Compute shaders may write storage buffers.
struct ComputePipelineDesc {
    std::span<const std::uint32_t> shader;
    std::string name;
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
    /// Undefined for a pipeline that only writes depth.
    Format colorFormat = Format::Undefined;
    /// The formats of further color targets, written by the fragment shader's outputs 1 and up, and
    /// blended like the first. At most kMaxColorTargets in all.
    std::vector<Format> moreColorFormats;
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
