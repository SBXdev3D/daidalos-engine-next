// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// One test per row of the math conventions table (CONVENTIONS.md, section 5).

#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

#include "MathTestUtilities.hpp"

using namespace daidalos;
using test::NearlyEqual;

namespace {

const float kQuarterTurn = glm::radians(90.0f);

} // namespace

TEST(MathConventions, AxesAreRightHanded) {
    // Right-handed: X cross Y = Z. Forward is -Z, so right cross up points backwards.
    EXPECT_TRUE(NearlyEqual(glm::cross(math::kRight, math::kUp), -math::kForward));
    EXPECT_TRUE(NearlyEqual(math::kForward, glm::vec3(0.0f, 0.0f, -1.0f)));
    EXPECT_TRUE(NearlyEqual(math::kUp, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST(MathConventions, PositiveRotationIsCounterClockwiseLookingDownTheAxis) {
    // Looking from +Z towards the origin, a positive rotation around Z turns +X towards +Y.
    const glm::quat rotation = glm::angleAxis(kQuarterTurn, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(NearlyEqual(rotation * math::kRight, math::kUp));
}

TEST(MathConventions, QuaternionComponentsAreWFirst) {
    const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_EQ(identity.w, 1.0f);
    EXPECT_EQ(identity.x, 0.0f);
    EXPECT_EQ(math::Transform{}.rotation, identity);
}

TEST(MathConventions, LocalMatrixScalesThenRotatesThenTranslates) {
    math::Transform transform;
    transform.scale = glm::vec3(2.0f);
    transform.rotation = glm::angleAxis(kQuarterTurn, math::kUp);
    transform.translation = glm::vec3(0.0f, 0.0f, 5.0f);

    // (1,0,0) scaled to (2,0,0), rotated a quarter turn left to (0,0,-2), then moved to (0,0,3).
    EXPECT_TRUE(NearlyEqual(math::TransformPoint(math::ToMatrix(transform), math::kRight), {0.0f, 0.0f, 3.0f}));
}

TEST(MathConventions, MatrixEqualsTranslateRotateScaleProduct) {
    math::Transform transform;
    transform.translation = {1.0f, -2.0f, 3.0f};
    transform.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
    transform.scale = {2.0f, 3.0f, 4.0f};

    const glm::mat4 expected = glm::translate(glm::mat4(1.0f), transform.translation) *
                               glm::mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
    const glm::mat4 actual = math::ToMatrix(transform);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[column][row], expected[column][row], test::kTolerance) << column << ", " << row;
        }
    }
}

TEST(MathConventions, MatricesAreColumnMajorWithTranslationInLastColumn) {
    math::Transform transform;
    transform.translation = {1.0f, 2.0f, 3.0f};
    const glm::mat4 matrix = math::ToMatrix(transform);
    EXPECT_TRUE(NearlyEqual(glm::vec3(matrix[3]), transform.translation));
    EXPECT_EQ(matrix[3][3], 1.0f);
}

TEST(MathConventions, ParentTransformAppliesAfterChildTransform) {
    // world = parentWorld * local: a child one meter ahead of a parent turned left ends up to the left.
    math::Transform parent;
    parent.rotation = glm::angleAxis(kQuarterTurn, math::kUp);
    math::Transform child;
    child.translation = math::kForward;

    const glm::mat4 world = math::ToMatrix(parent) * math::ToMatrix(child);
    EXPECT_TRUE(NearlyEqual(math::TransformPoint(world, glm::vec3(0.0f)), -math::kRight));
}

TEST(MathConventions, DirectionsIgnoreTranslation) {
    math::Transform transform;
    transform.translation = {10.0f, 20.0f, 30.0f};
    EXPECT_TRUE(NearlyEqual(math::TransformDirection(math::ToMatrix(transform), math::kForward), math::kForward));
}

TEST(MathConventions, PositiveYawTurnsLeft) {
    const glm::quat rotation = math::FromYawPitchRoll(kQuarterTurn, 0.0f, 0.0f);
    EXPECT_TRUE(NearlyEqual(rotation * math::kForward, -math::kRight));
}

TEST(MathConventions, PositivePitchLooksUp) {
    const glm::quat rotation = math::FromYawPitchRoll(0.0f, kQuarterTurn, 0.0f);
    EXPECT_TRUE(NearlyEqual(rotation * math::kForward, math::kUp));
}

TEST(MathConventions, PositiveRollTiltsUpTowardsTheLeft) {
    const glm::quat rotation = math::FromYawPitchRoll(0.0f, 0.0f, kQuarterTurn);
    EXPECT_TRUE(NearlyEqual(rotation * math::kUp, -math::kRight));
    EXPECT_TRUE(NearlyEqual(rotation * math::kForward, math::kForward));
}

TEST(MathConventions, YawIsAppliedBeforePitch) {
    // Yaw left, then pitch up around the turned right axis: the camera looks up and to the left.
    const glm::quat rotation = math::FromYawPitchRoll(kQuarterTurn, glm::radians(45.0f), 0.0f);
    const float half = std::sqrt(0.5f);
    EXPECT_TRUE(NearlyEqual(rotation * math::kForward, {-half, half, 0.0f}));
}
