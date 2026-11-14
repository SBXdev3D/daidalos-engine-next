// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/rhi/Types.hpp>

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <string>

namespace daidalos::rhi {

/// Triangles for a bottom-level structure: 32-bit indices into vertices whose first 12 bytes are the
/// position, three floats. The buffers need BufferUsage::AccelerationStructureInput.
struct TriangleGeometry {
    BufferHandle vertices;
    std::uint64_t vertexOffset = 0;
    std::uint32_t vertexStride = 0;
    std::uint32_t vertexCount = 0;
    BufferHandle indices;
    std::uint64_t indexOffset = 0;
    std::uint32_t indexCount = 0;

    bool operator==(const TriangleGeometry&) const = default;
};

enum class AccelerationStructureLevel {
    /// The triangles of one mesh.
    Bottom,
    /// Instances of bottom-level structures, which shaders trace rays against.
    Top,
};

struct AccelerationStructureDesc {
    AccelerationStructureLevel level = AccelerationStructureLevel::Bottom;
    /// Bottom level only.
    TriangleGeometry geometry;
    /// Top level only: the most instances a build can hold.
    std::uint32_t maxInstances = 0;
    /// Built again every frame: favors build speed over tracing speed.
    bool rebuiltEveryFrame = false;
    /// Bottom level: every build after the first refits it, moving its triangles without rebuilding its
    /// hierarchy. Much faster for geometry that moves every frame, such as skinned meshes, but rays slow
    /// down as the triangles move far from where the first build put them.
    bool refit = false;
    std::string name;
};

/// One instance of a top-level structure (Device::WriteInstances).
struct AccelerationStructureInstance {
    /// Object to world. Its last row is ignored.
    glm::mat4 transform{1.0f};
    /// What shaders read as the instance's custom index: 24 bits.
    std::uint32_t customIndex = 0;
    /// A ray sees the instance if the mask of its query and this mask share a bit.
    std::uint8_t mask = 0xFF;
    /// Opaque instances never stop a query for a candidate hit; others do, for alpha testing.
    bool opaque = true;
    /// Both faces of the triangles can be hit, whatever the query's culling.
    bool doubleSided = false;
    /// A bottom-level structure.
    AccelerationStructureHandle structure;
};

/// Bytes of one instance in a buffer given to a top-level build.
inline constexpr std::uint64_t kAccelerationStructureInstanceSize = 64;

} // namespace daidalos::rhi
