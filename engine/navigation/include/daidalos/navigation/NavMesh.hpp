// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace daidalos::navigation {

/// Who walks the mesh, and how fine the mesh is (ADR-0069). Meters and degrees.
struct NavConfig {
    /// The voxels the geometry is cut into, across and up.
    float cellSize = 0.2f;
    float cellHeight = 0.1f;
    /// The walker: how tall, how wide, how high a step it climbs, how steep a slope it walks.
    float agentHeight = 1.8f;
    float agentRadius = 0.35f;
    float agentClimb = 0.45f;
    float maxSlope = 45.0f;
};

/// A box in world space: a door's, which the mesh cuts its polygons at, so that it can shut them.
struct NavBox {
    glm::vec3 low{0.0f};
    glm::vec3 high{0.0f};
};

/// Where walkers can go, and the ways between two points (ADR-0069): Recast builds it from the level's
/// triangles, and Detour finds paths on it. Doors block the polygons they stand on while they are shut.
class NavMesh {
public:
    /// From triangles in world space, three indices each, and the boxes of the doors, whose polygons stay
    /// apart for SetBlocked.
    [[nodiscard]] static core::Result<NavMesh> Build(std::span<const glm::vec3> positions,
                                                     std::span<const std::uint32_t> indices,
                                                     std::span<const NavBox> doors = {}, const NavConfig& config = {});

    NavMesh(NavMesh&& other) noexcept;
    NavMesh& operator=(NavMesh&& other) noexcept;
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;
    ~NavMesh();

    /// The corners of the shortest way from one point to another, the first being the start and the last the
    /// end, or the nearest reachable point to it. Empty when either is off the mesh.
    [[nodiscard]] std::vector<glm::vec3> FindPath(const glm::vec3& from, const glm::vec3& to) const;
    /// The nearest point of the mesh, within a reach across and up.
    [[nodiscard]] std::optional<glm::vec3> NearestPoint(const glm::vec3& point,
                                                        const glm::vec3& reach = {2.0f, 4.0f, 2.0f}) const;
    /// Shuts or opens the door polygons whose centers are in a box: a door given to Build, closed or open.
    void SetBlocked(const glm::vec3& low, const glm::vec3& high, bool blocked);

    [[nodiscard]] std::size_t PolygonCount() const;
    /// Seconds the build took.
    [[nodiscard]] double BuildSeconds() const;

private:
    struct Impl;
    explicit NavMesh(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::navigation
