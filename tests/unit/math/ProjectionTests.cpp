// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/math/Projection.hpp>

#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include "MathTestUtilities.hpp"

using namespace daidalos;

namespace {

constexpr float kNear = 0.1f;
constexpr float kFar = 100.0f;
const float kFov = glm::radians(90.0f);

// Projects a view-space point and returns normalized device coordinates (after the divide by w).
glm::vec3 ToNdc(const glm::mat4& projection, const glm::vec3& viewPoint) {
    const glm::vec4 clip = projection * glm::vec4(viewPoint, 1.0f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

TEST(Projection, NearPlaneMapsToOneAndFarPlaneToZero) {
    const glm::mat4 projection = math::PerspectiveReverseZ(kFov, 1.0f, kNear, kFar);
    EXPECT_NEAR(ToNdc(projection, {0.0f, 0.0f, -kNear}).z, 1.0f, test::kTolerance);
    EXPECT_NEAR(ToNdc(projection, {0.0f, 0.0f, -kFar}).z, 0.0f, test::kTolerance);
}

TEST(Projection, DepthDecreasesWithDistance) {
    const glm::mat4 projection = math::PerspectiveReverseZ(kFov, 1.0f, kNear, kFar);
    const float closer = ToNdc(projection, {0.0f, 0.0f, -1.0f}).z;
    const float farther = ToNdc(projection, {0.0f, 0.0f, -10.0f}).z;
    EXPECT_GT(closer, farther);
    EXPECT_GT(farther, 0.0f);
    EXPECT_LT(closer, 1.0f);
}

TEST(Projection, CameraLooksDownNegativeZ) {
    const glm::mat4 projection = math::PerspectiveReverseZ(kFov, 1.0f, kNear, kFar);
    // A point straight ahead projects to the center; a point behind the camera has a negative w.
    const glm::vec3 ahead = ToNdc(projection, {0.0f, 0.0f, -5.0f});
    EXPECT_NEAR(ahead.x, 0.0f, test::kTolerance);
    EXPECT_NEAR(ahead.y, 0.0f, test::kTolerance);
    EXPECT_LT((projection * glm::vec4(0.0f, 0.0f, 5.0f, 1.0f)).w, 0.0f);
}

TEST(Projection, RightAndUpStayPositiveInClipSpace) {
    // Clip-space Y points up: the Vulkan Y flip is done by the viewport, not by the matrix.
    const glm::mat4 projection = math::PerspectiveReverseZ(kFov, 1.0f, kNear, kFar);
    const glm::vec3 ndc = ToNdc(projection, {1.0f, 1.0f, -5.0f});
    EXPECT_GT(ndc.x, 0.0f);
    EXPECT_GT(ndc.y, 0.0f);
}

TEST(Projection, FieldOfViewEdgeMapsToNdcEdge) {
    // With a 90 degree vertical field of view, a point at 45 degrees above the axis is on the top edge.
    const glm::mat4 projection = math::PerspectiveReverseZ(kFov, 2.0f, kNear, kFar);
    EXPECT_NEAR(ToNdc(projection, {0.0f, 3.0f, -3.0f}).y, 1.0f, test::kTolerance);
    // The aspect ratio widens the horizontal field: x = 2 * distance is on the right edge.
    EXPECT_NEAR(ToNdc(projection, {6.0f, 0.0f, -3.0f}).x, 1.0f, test::kTolerance);
}

TEST(Projection, InfiniteFarPlaneKeepsDepthAboveZero) {
    const glm::mat4 projection = math::InfinitePerspectiveReverseZ(kFov, 1.0f, kNear);
    EXPECT_NEAR(ToNdc(projection, {0.0f, 0.0f, -kNear}).z, 1.0f, test::kTolerance);
    const float distant = ToNdc(projection, {0.0f, 0.0f, -1.0e6f}).z;
    EXPECT_GT(distant, 0.0f);
    EXPECT_LT(distant, 1.0e-6f);
}

TEST(Projection, InfiniteMatchesFiniteForVeryDistantFarPlane) {
    const glm::mat4 finite = math::PerspectiveReverseZ(kFov, 1.5f, kNear, 1.0e7f);
    const glm::mat4 infinite = math::InfinitePerspectiveReverseZ(kFov, 1.5f, kNear);
    const glm::vec3 point{1.0f, -2.0f, -20.0f};
    EXPECT_TRUE(test::NearlyEqual(ToNdc(finite, point), ToNdc(infinite, point)));
}
