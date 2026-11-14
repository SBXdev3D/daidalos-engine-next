// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/math/Aabb.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

#include "MathTestUtilities.hpp"

using namespace daidalos;
using math::Aabb;
using test::NearlyEqual;

TEST(Aabb, DefaultIsEmpty) {
    EXPECT_TRUE(Aabb{}.IsEmpty());
    EXPECT_FALSE(Aabb{}.Contains(glm::vec3(0.0f)));
}

TEST(Aabb, ExpandingEmptyBoxByOnePointGivesThatPoint) {
    Aabb box;
    box.Expand(glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(box.IsEmpty());
    EXPECT_TRUE(NearlyEqual(box.min, {1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(NearlyEqual(box.max, {1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(box.Contains({1.0f, 2.0f, 3.0f}));
}

TEST(Aabb, ExpandByPointsCoversAllPoints) {
    Aabb box;
    box.Expand(glm::vec3(1.0f, -1.0f, 0.0f));
    box.Expand(glm::vec3(-2.0f, 3.0f, 5.0f));
    EXPECT_TRUE(NearlyEqual(box.min, {-2.0f, -1.0f, 0.0f}));
    EXPECT_TRUE(NearlyEqual(box.max, {1.0f, 3.0f, 5.0f}));
}

TEST(Aabb, ExpandByEmptyBoxChangesNothing) {
    Aabb box = Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f));
    const Aabb before = box;
    box.Expand(Aabb{});
    EXPECT_EQ(box, before);
}

TEST(Aabb, ExpandByBoxGivesTheUnion) {
    Aabb box = Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f));
    box.Expand(Aabb::FromCenterExtents(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(1.0f)));
    EXPECT_TRUE(NearlyEqual(box.min, {-1.0f, -1.0f, -1.0f}));
    EXPECT_TRUE(NearlyEqual(box.max, {6.0f, 1.0f, 1.0f}));
}

TEST(Aabb, CenterAndExtents) {
    const Aabb box{{-1.0f, 0.0f, 2.0f}, {3.0f, 4.0f, 4.0f}};
    EXPECT_TRUE(NearlyEqual(box.Center(), {1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(NearlyEqual(box.Extents(), {2.0f, 2.0f, 1.0f}));
}

TEST(Aabb, ContainsIncludesTheBoundary) {
    const Aabb box = Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f));
    EXPECT_TRUE(box.Contains({1.0f, 1.0f, 1.0f}));
    EXPECT_TRUE(box.Contains({0.0f, -1.0f, 0.5f}));
    EXPECT_FALSE(box.Contains({1.001f, 0.0f, 0.0f}));
}

TEST(Aabb, IntersectsWhenOverlappingOrTouching) {
    const Aabb box = Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f));
    EXPECT_TRUE(box.Intersects(Aabb::FromCenterExtents({1.5f, 0.0f, 0.0f}, glm::vec3(1.0f))));
    EXPECT_TRUE(box.Intersects(Aabb::FromCenterExtents({2.0f, 0.0f, 0.0f}, glm::vec3(1.0f))));
    EXPECT_FALSE(box.Intersects(Aabb::FromCenterExtents({2.5f, 0.0f, 0.0f}, glm::vec3(1.0f))));
    EXPECT_FALSE(box.Intersects(Aabb::FromCenterExtents({0.0f, 0.0f, -3.0f}, glm::vec3(1.0f))));
}

TEST(Aabb, TransformTranslatesAndScales) {
    math::Transform transform;
    transform.translation = {10.0f, 0.0f, 0.0f};
    transform.scale = {2.0f, 1.0f, 1.0f};
    const Aabb box =
        math::TransformAabb(Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f)), math::ToMatrix(transform));
    EXPECT_TRUE(NearlyEqual(box.min, {8.0f, -1.0f, -1.0f}));
    EXPECT_TRUE(NearlyEqual(box.max, {12.0f, 1.0f, 1.0f}));
}

TEST(Aabb, TransformOfRotatedBoxEnclosesTheCorners) {
    // A unit cube turned 45 degrees around Y is sqrt(2) wide along X and Z.
    math::Transform transform;
    transform.rotation = glm::angleAxis(glm::radians(45.0f), math::kUp);
    const Aabb box =
        math::TransformAabb(Aabb::FromCenterExtents(glm::vec3(0.0f), glm::vec3(1.0f)), math::ToMatrix(transform));
    const float extent = std::sqrt(2.0f);
    EXPECT_TRUE(NearlyEqual(box.Extents(), {extent, 1.0f, extent}));
    EXPECT_TRUE(NearlyEqual(box.Center(), glm::vec3(0.0f)));
}

TEST(Aabb, TransformOfEmptyBoxIsEmpty) {
    math::Transform transform;
    transform.translation = {1.0f, 2.0f, 3.0f};
    EXPECT_TRUE(math::TransformAabb(Aabb{}, math::ToMatrix(transform)).IsEmpty());
}
