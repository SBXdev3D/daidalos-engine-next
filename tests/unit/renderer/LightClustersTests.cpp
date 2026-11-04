// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <vector>

#include "LightClusters.hpp"

using namespace daidalos;
using renderer::ClusterCamera;
using renderer::Light;
using renderer::LightClusters;

namespace {

// A camera at (1, 2, 8), turned a little, and lights scattered around it, some behind it.
ClusterCamera TestCamera() {
    ClusterCamera camera;
    const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 8.0f)),
                                        glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.view = glm::inverse(world);
    camera.verticalFov = glm::radians(60.0f);
    camera.aspect = 16.0f / 9.0f;
    camera.nearPlane = 0.1f;
    return camera;
}

std::vector<Light> ScatteredLights(std::uint32_t count) {
    std::minstd_rand random(7);
    std::uniform_real_distribution<float> position(-30.0f, 30.0f);
    std::uniform_real_distribution<float> range(0.5f, 15.0f);
    std::vector<Light> lights(count);
    for (Light& light : lights) {
        light.position = {position(random), position(random) * 0.3f, position(random)};
        light.range = range(random);
    }
    return lights;
}

std::set<std::uint32_t> LightsOf(const LightClusters& clusters, std::uint32_t cluster) {
    const glm::uvec2 range = clusters.ranges[cluster];
    return {clusters.lightIndices.begin() + range.x, clusters.lightIndices.begin() + range.x + range.y};
}

} // namespace

TEST(LightClusters, MatchesTheBruteForceReference) {
    // The reference tests every light against every cluster, through the cluster each point of a dense
    // sampling of the light's sphere falls in: every cluster the light reaches must list it.
    const ClusterCamera camera = TestCamera();
    const std::vector<Light> lights = ScatteredLights(40);
    LightClusters clusters;
    renderer::AssignLightsToClusters(lights, camera, clusters);
    ASSERT_EQ(clusters.ranges.size(), renderer::kClusterCount);

    std::minstd_rand random(11);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    for (std::uint32_t index = 0; index < lights.size(); ++index) {
        const glm::vec3 center = glm::vec3(camera.view * glm::vec4(lights[index].position, 1.0f));
        for (int sample = 0; sample < 2000; ++sample) {
            const glm::vec3 offset(unit(random), unit(random), unit(random));
            if (glm::dot(offset, offset) > 1.0f) {
                continue;
            }
            const glm::vec3 point = center + offset * lights[index].range;
            const float up = std::tan(camera.verticalFov * 0.5f);
            const bool onScreen =
                std::abs(point.x) <= -point.z * up * camera.aspect && std::abs(point.y) <= -point.z * up;
            if (-point.z < camera.nearPlane || !onScreen) {
                continue; // Never drawn.
            }
            EXPECT_TRUE(LightsOf(clusters, renderer::ClusterOf(point, camera)).contains(index))
                << "light " << index << " reaches a point whose cluster does not list it";
        }
    }
}

TEST(LightClusters, EveryPointSeesTheLightsThatReachIt) {
    const ClusterCamera camera = TestCamera();
    const std::vector<Light> lights = ScatteredLights(60);
    LightClusters clusters;
    renderer::AssignLightsToClusters(lights, camera, clusters);

    std::minstd_rand random(3);
    std::uniform_real_distribution<float> across(-1.0f, 1.0f);
    std::uniform_real_distribution<float> depth(0.1f, 160.0f);
    const float up = std::tan(camera.verticalFov * 0.5f);
    const glm::mat4 world = glm::inverse(camera.view);
    for (int sample = 0; sample < 20000; ++sample) {
        // A point inside the view, anywhere from the near plane to beyond the last slice.
        const float d = depth(random);
        const glm::vec3 viewPoint(across(random) * up * camera.aspect * d, across(random) * up * d, -d);
        const glm::vec3 worldPoint = glm::vec3(world * glm::vec4(viewPoint, 1.0f));
        const std::set<std::uint32_t> listed = LightsOf(clusters, renderer::ClusterOf(viewPoint, camera));
        for (std::uint32_t index = 0; index < lights.size(); ++index) {
            if (glm::distance(lights[index].position, worldPoint) < lights[index].range) {
                ASSERT_TRUE(listed.contains(index)) << "light " << index << " at depth " << d;
            }
        }
    }
}

TEST(LightClusters, LightsListOnlyTheClustersNearThem) {
    // A small light in front of the camera lists only a few clusters, far from all of them.
    ClusterCamera camera = TestCamera();
    camera.view = glm::mat4(1.0f);
    const std::vector<Light> lights{{.position = {0.0f, 0.0f, -10.0f}, .range = 1.0f}};
    LightClusters clusters;
    renderer::AssignLightsToClusters(lights, camera, clusters);
    EXPECT_GT(clusters.lightIndices.size(), 0U);
    EXPECT_LT(clusters.lightIndices.size(), 40U);
}

TEST(LightClusters, SlicesGrowWithDepthAndStopAtTheLast) {
    EXPECT_EQ(renderer::ClusterSlice(0.1f, 0.1f, 128.0f), 0U);
    EXPECT_LT(renderer::ClusterSlice(1.0f, 0.1f, 128.0f), renderer::ClusterSlice(10.0f, 0.1f, 128.0f));
    EXPECT_EQ(renderer::ClusterSlice(1000.0f, 0.1f, 128.0f), renderer::kClusterSlices - 1);
}

TEST(LightClusters, LightBeyondTheFarPlaneReachesNoCluster) {
    // At the origin, looking down -Z.
    ClusterCamera camera = TestCamera();
    camera.view = glm::mat4(1.0f);
    camera.farPlane = 50.0f;
    const std::vector<Light> lights{{.position = {0.0f, 0.0f, -60.0f}, .range = 5.0f},
                                    {.position = {0.0f, 0.0f, -52.0f}, .range = 5.0f}};
    LightClusters clusters;
    renderer::AssignLightsToClusters(lights, camera, clusters);
    // Only the second light, whose sphere crosses the far plane, is listed.
    EXPECT_FALSE(clusters.lightIndices.empty());
    EXPECT_EQ(std::ranges::count(clusters.lightIndices, 0U), 0);
}
