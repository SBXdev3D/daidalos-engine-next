// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/navigation/NavMesh.hpp>

#include <glm/vector_relational.hpp>

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>

namespace daidalos::navigation {

namespace {

constexpr std::string_view kModule = "navigation";
// Polygon flags: walkable, and shut by a door.
constexpr unsigned short kWalk = 1;
constexpr unsigned short kShut = 2;
constexpr int kMaxPolys = 1024;
// The area of door polygons.
constexpr unsigned char kDoorArea = 2;
constexpr int kMaxNodes = 4096;

core::Error Fail(const std::string& message) {
    return core::Error{core::ErrorCode::InvalidArgument, kModule, message};
}

// Frees what Recast allocates.
struct Heightfield {
    rcHeightfield* value = rcAllocHeightfield();
    ~Heightfield() { rcFreeHeightField(value); }
};
struct Compact {
    rcCompactHeightfield* value = rcAllocCompactHeightfield();
    ~Compact() { rcFreeCompactHeightfield(value); }
};
struct Contours {
    rcContourSet* value = rcAllocContourSet();
    ~Contours() { rcFreeContourSet(value); }
};
struct PolyMesh {
    rcPolyMesh* value = rcAllocPolyMesh();
    ~PolyMesh() { rcFreePolyMesh(value); }
};
struct Detail {
    rcPolyMeshDetail* value = rcAllocPolyMeshDetail();
    ~Detail() { rcFreePolyMeshDetail(value); }
};

} // namespace

struct NavMesh::Impl {
    ~Impl() {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtQueryFilter filter;
    double seconds = 0.0;
};

NavMesh::NavMesh(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
NavMesh::NavMesh(NavMesh&& other) noexcept = default;
NavMesh& NavMesh::operator=(NavMesh&& other) noexcept = default;
NavMesh::~NavMesh() = default;

core::Result<NavMesh> NavMesh::Build(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                                     std::span<const NavBox> doors, const NavConfig& config) {
    DAI_PROFILE_SCOPE("navigation::Build");
    const auto start = std::chrono::steady_clock::now();
    if (positions.empty() || indices.size() < 3) {
        return tl::unexpected(Fail("A navigation mesh needs triangles"));
    }
    const auto* vertices = reinterpret_cast<const float*>(positions.data());
    const int vertexCount = static_cast<int>(positions.size());
    std::vector<int> triangles(indices.begin(), indices.end());
    const int triangleCount = static_cast<int>(triangles.size() / 3);

    rcConfig cfg{};
    cfg.cs = config.cellSize;
    cfg.ch = config.cellHeight;
    cfg.walkableSlopeAngle = config.maxSlope;
    cfg.walkableHeight = static_cast<int>(std::ceil(config.agentHeight / cfg.ch));
    cfg.walkableClimb = static_cast<int>(std::floor(config.agentClimb / cfg.ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(config.agentRadius / cfg.cs));
    cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = 8 * 8;
    cfg.mergeRegionArea = 20 * 20;
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = cfg.cs * 6.0f;
    cfg.detailSampleMaxError = cfg.ch;
    rcCalcBounds(vertices, vertexCount, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcContext context(false);
    Heightfield solid;
    if (!rcCreateHeightfield(&context, *solid.value, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        return tl::unexpected(Fail("Cannot make the height field"));
    }
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount), 0);
    rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, vertices, vertexCount, triangles.data(), triangleCount,
                            areas.data());
    if (!rcRasterizeTriangles(&context, vertices, vertexCount, triangles.data(), areas.data(), triangleCount,
                              *solid.value, cfg.walkableClimb)) {
        return tl::unexpected(Fail("Cannot rasterize the level"));
    }
    rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid.value);
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid.value);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid.value);

    Compact compact;
    if (!rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid.value, *compact.value) ||
        !rcErodeWalkableArea(&context, cfg.walkableRadius, *compact.value)) {
        return tl::unexpected(Fail("Cannot find the walkable regions"));
    }
    // Doors are an area of their own: their polygons stop at the door's box.
    for (const NavBox& door : doors) {
        rcMarkBoxArea(&context, &door.low.x, &door.high.x, kDoorArea, *compact.value);
    }
    if (!rcBuildDistanceField(&context, *compact.value) ||
        !rcBuildRegions(&context, *compact.value, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
        return tl::unexpected(Fail("Cannot find the walkable regions"));
    }
    Contours contours;
    PolyMesh polygons;
    Detail detail;
    if (!rcBuildContours(&context, *compact.value, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours.value) ||
        !rcBuildPolyMesh(&context, *contours.value, cfg.maxVertsPerPoly, *polygons.value) ||
        !rcBuildPolyMeshDetail(&context, *polygons.value, *compact.value, cfg.detailSampleDist,
                               cfg.detailSampleMaxError, *detail.value)) {
        return tl::unexpected(Fail("Cannot make the polygons"));
    }
    if (polygons.value->npolys == 0) {
        return tl::unexpected(Fail("Nothing to walk on"));
    }
    for (int index = 0; index < polygons.value->npolys; ++index) {
        const unsigned char area = polygons.value->areas[index];
        polygons.value->flags[index] = area == RC_WALKABLE_AREA || area == kDoorArea ? kWalk : 0;
    }

    dtNavMeshCreateParams params{};
    params.verts = polygons.value->verts;
    params.vertCount = polygons.value->nverts;
    params.polys = polygons.value->polys;
    params.polyAreas = polygons.value->areas;
    params.polyFlags = polygons.value->flags;
    params.polyCount = polygons.value->npolys;
    params.nvp = polygons.value->nvp;
    params.detailMeshes = detail.value->meshes;
    params.detailVerts = detail.value->verts;
    params.detailVertsCount = detail.value->nverts;
    params.detailTris = detail.value->tris;
    params.detailTriCount = detail.value->ntris;
    params.walkableHeight = config.agentHeight;
    params.walkableRadius = config.agentRadius;
    params.walkableClimb = config.agentClimb;
    rcVcopy(params.bmin, polygons.value->bmin);
    rcVcopy(params.bmax, polygons.value->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size)) {
        return tl::unexpected(Fail("Cannot make the navigation data"));
    }
    auto impl = std::make_unique<Impl>();
    impl->mesh = dtAllocNavMesh();
    if (impl->mesh == nullptr || dtStatusFailed(impl->mesh->init(data, size, DT_TILE_FREE_DATA))) {
        dtFree(data);
        return tl::unexpected(Fail("Cannot start the navigation mesh"));
    }
    impl->query = dtAllocNavMeshQuery();
    if (impl->query == nullptr || dtStatusFailed(impl->query->init(impl->mesh, kMaxNodes))) {
        return tl::unexpected(Fail("Cannot start the path queries"));
    }
    impl->filter.setIncludeFlags(kWalk);
    impl->filter.setExcludeFlags(kShut);
    impl->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return NavMesh(std::move(impl));
}

std::vector<glm::vec3> NavMesh::FindPath(const glm::vec3& from, const glm::vec3& to) const {
    const Impl& impl = *m_impl;
    constexpr std::array<float, 3> kReach{2.0f, 4.0f, 2.0f};
    dtPolyRef start = 0;
    dtPolyRef end = 0;
    std::array<float, 3> startPoint{};
    std::array<float, 3> endPoint{};
    impl.query->findNearestPoly(&from.x, kReach.data(), &impl.filter, &start, startPoint.data());
    impl.query->findNearestPoly(&to.x, kReach.data(), &impl.filter, &end, endPoint.data());
    if (start == 0 || end == 0) {
        return {};
    }
    std::array<dtPolyRef, kMaxPolys> corridor{};
    int corridorSize = 0;
    impl.query->findPath(start, end, startPoint.data(), endPoint.data(), &impl.filter, corridor.data(), &corridorSize,
                         kMaxPolys);
    if (corridorSize == 0) {
        return {};
    }
    // When the end cannot be reached, the corridor stops at the nearest polygon to it.
    if (corridor[static_cast<std::size_t>(corridorSize - 1)] != end) {
        impl.query->closestPointOnPoly(corridor[static_cast<std::size_t>(corridorSize - 1)], &to.x, endPoint.data(),
                                       nullptr);
    }
    std::array<float, kMaxPolys * 3> straight{};
    int straightSize = 0;
    impl.query->findStraightPath(startPoint.data(), endPoint.data(), corridor.data(), corridorSize, straight.data(),
                                 nullptr, nullptr, &straightSize, kMaxPolys);
    std::vector<glm::vec3> path;
    path.reserve(static_cast<std::size_t>(straightSize));
    for (int index = 0; index < straightSize; ++index) {
        path.emplace_back(straight[static_cast<std::size_t>(index) * 3],
                          straight[static_cast<std::size_t>(index) * 3 + 1],
                          straight[static_cast<std::size_t>(index) * 3 + 2]);
    }
    return path;
}

std::optional<glm::vec3> NavMesh::NearestPoint(const glm::vec3& point, const glm::vec3& reach) const {
    dtPolyRef polygon = 0;
    glm::vec3 nearest{0.0f};
    m_impl->query->findNearestPoly(&point.x, &reach.x, &m_impl->filter, &polygon, &nearest.x);
    return polygon != 0 ? std::optional<glm::vec3>(nearest) : std::nullopt;
}

void NavMesh::SetBlocked(const glm::vec3& low, const glm::vec3& high, bool blocked) {
    const dtNavMesh& mesh = *m_impl->mesh;
    for (int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex) {
        const dtMeshTile* tile = mesh.getTile(tileIndex);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        const dtPolyRef base = mesh.getPolyRefBase(tile);
        for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
            const dtPoly& poly = tile->polys[polyIndex];
            if (poly.getArea() != kDoorArea) {
                continue;
            }
            glm::vec3 center{0.0f};
            for (int vertex = 0; vertex < poly.vertCount; ++vertex) {
                const float* at = &tile->verts[poly.verts[vertex] * 3];
                center += glm::vec3(at[0], at[1], at[2]);
            }
            center /= static_cast<float>(std::max<int>(poly.vertCount, 1));
            if (glm::all(glm::greaterThanEqual(center, low)) && glm::all(glm::lessThanEqual(center, high))) {
                const dtPolyRef reference = base | static_cast<dtPolyRef>(polyIndex);
                unsigned short flags = 0;
                m_impl->mesh->getPolyFlags(reference, &flags);
                m_impl->mesh->setPolyFlags(reference, blocked ? (flags | kShut) : (flags & ~kShut));
            }
        }
    }
}

std::size_t NavMesh::PolygonCount() const {
    std::size_t count = 0;
    const dtNavMesh& mesh = *m_impl->mesh;
    for (int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex) {
        if (const dtMeshTile* tile = mesh.getTile(tileIndex); tile != nullptr && tile->header != nullptr) {
            count += static_cast<std::size_t>(tile->header->polyCount);
        }
    }
    return count;
}

std::vector<glm::vec3> NavMesh::Edges() const {
    std::vector<glm::vec3> edges;
    const dtNavMesh& mesh = *m_impl->mesh;
    for (int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex) {
        const dtMeshTile* tile = mesh.getTile(tileIndex);
        if (tile == nullptr || tile->header == nullptr) {
            continue;
        }
        for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
            const dtPoly& poly = tile->polys[polyIndex];
            if (poly.getType() != DT_POLYTYPE_GROUND) {
                continue;
            }
            for (int vertex = 0; vertex < poly.vertCount; ++vertex) {
                const float* from = &tile->verts[poly.verts[vertex] * 3];
                const float* to = &tile->verts[poly.verts[(vertex + 1) % poly.vertCount] * 3];
                edges.emplace_back(from[0], from[1], from[2]);
                edges.emplace_back(to[0], to[1], to[2]);
            }
        }
    }
    return edges;
}

double NavMesh::BuildSeconds() const {
    return m_impl->seconds;
}

} // namespace daidalos::navigation
