// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/renderer/SceneView.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace daidalos::renderer {

/// Clustered light assignment, on the CPU (ADR-0008, amended). The view is cut into kClusterColumns by
/// kClusterRows tiles of the screen, and each tile into kClusterSlices slices of depth, spaced
/// exponentially from the near plane to kClusterFar. A pixel reads the lights of its cluster only.
inline constexpr std::uint32_t kClusterColumns = 16;
inline constexpr std::uint32_t kClusterRows = 9;
inline constexpr std::uint32_t kClusterSlices = 24;
inline constexpr std::uint32_t kClusterCount = kClusterColumns * kClusterRows * kClusterSlices;
/// Meters. Farther pixels use the last slice.
inline constexpr float kClusterFar = 128.0f;

/// The camera the clusters follow.
struct ClusterCamera {
    /// World space to camera space; the camera looks down -Z.
    glm::mat4 view{1.0f};
    float verticalFov = 1.0f;
    /// Width / height.
    float aspect = 1.0f;
    float nearPlane = 0.1f;
};

struct LightClusters {
    /// For each cluster, the first of its lights in lightIndices, and their count.
    std::vector<glm::uvec2> ranges;
    /// Indices into the view's lights.
    std::vector<std::uint32_t> lightIndices;
    /// Work space of AssignLightsToClusters: each cluster that a light reaches, with the light.
    std::vector<glm::uvec2> hits;
};

/// Lists, for each cluster, the lights whose range reaches into it. Conservative: a light that reaches a
/// point is always in the list of that point's cluster. The vectors of clusters are reused.
void AssignLightsToClusters(std::span<const Light> lights, const ClusterCamera& camera, LightClusters& clusters);

/// The depth slice of a view depth (meters in front of the camera), as the shader computes it.
[[nodiscard]] std::uint32_t ClusterSlice(float viewDepth, float nearPlane);

/// The cluster of a point in camera space, as the shader finds it from the pixel and the depth.
[[nodiscard]] std::uint32_t ClusterOf(const glm::vec3& viewPoint, const ClusterCamera& camera);

/// The scale of the depth slices in the shader: slice = log(viewDepth / nearPlane) * scale.
[[nodiscard]] float ClusterSliceScale(float nearPlane);

} // namespace daidalos::renderer
