// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "LightClusters.hpp"

#include <daidalos/core/Profile.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace daidalos::renderer {

namespace {

/// Where a slice starts, in meters from the camera.
float SliceDepth(std::uint32_t slice, float nearPlane, float farPlane) {
    return nearPlane * std::pow(farPlane / nearPlane, static_cast<float>(slice) / kClusterSlices);
}

/// The box in camera space around a cluster: its tile of the screen between two depths.
struct Box {
    glm::vec3 min;
    glm::vec3 max;
};

Box ClusterBox(std::uint32_t column, std::uint32_t row, float nearDepth, float farDepth, const glm::vec2& tangents) {
    // The tile's edges, from -1 to 1 across the screen. Rows count from the top, where Y is up.
    const float left = -1.0f + 2.0f * static_cast<float>(column) / kClusterColumns;
    const float right = -1.0f + 2.0f * static_cast<float>(column + 1) / kClusterColumns;
    const float top = 1.0f - 2.0f * static_cast<float>(row) / kClusterRows;
    const float bottom = 1.0f - 2.0f * static_cast<float>(row + 1) / kClusterRows;
    Box box{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (const float depth : {nearDepth, farDepth}) {
        for (const float x : {left, right}) {
            for (const float y : {bottom, top}) {
                const glm::vec3 corner(x * tangents.x * depth, y * tangents.y * depth, -depth);
                box.min = glm::min(box.min, corner);
                box.max = glm::max(box.max, corner);
            }
        }
    }
    return box;
}

bool SphereTouchesBox(const glm::vec3& center, float radius, const Box& box) {
    const glm::vec3 nearest = glm::clamp(center, box.min, box.max);
    const glm::vec3 offset = center - nearest;
    return glm::dot(offset, offset) <= radius * radius;
}

std::uint32_t ClusterIndex(std::uint32_t column, std::uint32_t row, std::uint32_t slice) {
    return column + row * kClusterColumns + slice * kClusterColumns * kClusterRows;
}

/// Half the size of the view at one meter, across and up.
glm::vec2 Tangents(const ClusterCamera& camera) {
    const float up = std::tan(camera.verticalFov * 0.5f);
    return {up * camera.aspect, up};
}

} // namespace

float ClusterSliceScale(float nearPlane, float farPlane) {
    return static_cast<float>(kClusterSlices) / std::log(farPlane / nearPlane);
}

std::uint32_t ClusterSlice(float viewDepth, float nearPlane, float farPlane) {
    const float slice = std::log(std::max(viewDepth, nearPlane) / nearPlane) * ClusterSliceScale(nearPlane, farPlane);
    return std::min(static_cast<std::uint32_t>(std::max(slice, 0.0f)), kClusterSlices - 1);
}

std::uint32_t ClusterOf(const glm::vec3& viewPoint, const ClusterCamera& camera) {
    const float depth = -viewPoint.z;
    const glm::vec2 tangents = Tangents(camera);
    // From -1 to 1 across the screen, then tiles, rows from the top.
    const float x = viewPoint.x / (depth * tangents.x);
    const float y = viewPoint.y / (depth * tangents.y);
    const auto column = static_cast<std::uint32_t>(
        std::clamp((x * 0.5f + 0.5f) * kClusterColumns, 0.0f, static_cast<float>(kClusterColumns - 1)));
    const auto row = static_cast<std::uint32_t>(
        std::clamp((0.5f - y * 0.5f) * kClusterRows, 0.0f, static_cast<float>(kClusterRows - 1)));
    return ClusterIndex(column, row, ClusterSlice(depth, camera.nearPlane, camera.farPlane));
}

void AssignLightsToClusters(std::span<const Light> lights, const ClusterCamera& camera, LightClusters& clusters) {
    DAI_PROFILE_FUNCTION();
    const glm::vec2 tangents = Tangents(camera);
    // Every (cluster, light) pair, in light order, then a counting sort packs them by cluster.
    std::vector<glm::uvec2>& hits = clusters.hits;
    hits.clear();
    for (std::uint32_t index = 0; index < lights.size(); ++index) {
        const Light& light = lights[index];
        const glm::vec3 center = glm::vec3(camera.view * glm::vec4(light.position, 1.0f));
        const float depth = -center.z;
        if (depth + light.range < camera.nearPlane || depth - light.range > camera.farPlane) {
            continue; // Entirely behind the camera, or beyond the far plane.
        }
        // Only the slices that the light's sphere can reach. The first slice reaches to the camera.
        const std::uint32_t first = ClusterSlice(depth - light.range, camera.nearPlane, camera.farPlane);
        const std::uint32_t last = ClusterSlice(depth + light.range, camera.nearPlane, camera.farPlane);
        for (std::uint32_t slice = first; slice <= last; ++slice) {
            const float sliceNear = slice == 0 ? 0.0f : SliceDepth(slice, camera.nearPlane, camera.farPlane);
            const float sliceFar = SliceDepth(slice + 1, camera.nearPlane, camera.farPlane);
            for (std::uint32_t row = 0; row < kClusterRows; ++row) {
                for (std::uint32_t column = 0; column < kClusterColumns; ++column) {
                    if (SphereTouchesBox(center, light.range, ClusterBox(column, row, sliceNear, sliceFar, tangents))) {
                        hits.emplace_back(ClusterIndex(column, row, slice), index);
                    }
                }
            }
        }
    }
    clusters.ranges.assign(kClusterCount, glm::uvec2(0));
    for (const glm::uvec2& hit : hits) {
        ++clusters.ranges[hit.x].y;
    }
    std::uint32_t offset = 0;
    for (glm::uvec2& range : clusters.ranges) {
        range.x = offset;
        offset += range.y;
        range.y = 0;
    }
    clusters.lightIndices.resize(hits.size());
    for (const glm::uvec2& hit : hits) {
        glm::uvec2& range = clusters.ranges[hit.x];
        clusters.lightIndices[range.x + range.y++] = hit.y;
    }
}

} // namespace daidalos::renderer
