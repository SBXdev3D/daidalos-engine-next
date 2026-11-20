// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include "Paint.hpp"

namespace {

using editor::PaintBrush;

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected) {
    EXPECT_LT(glm::distance(actual, expected), 1e-4f) << actual.x << ' ' << actual.y << ' ' << actual.z;
}

TEST(PaintTest, ADecalProjectsIntoTheSurface) {
    for (const glm::vec3 normal : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1),
                                   glm::normalize(glm::vec3(1, 1, 1))}) {
        const auto stamp = editor::Stamp(PaintBrush{}, glm::vec3(1, 2, 3), normal, 0.7f);
        ASSERT_TRUE(stamp.decal.has_value());
        ExpectNear(stamp.transform.rotation * glm::vec3(0, 0, -1), -normal);
        ExpectNear(stamp.transform.translation, glm::vec3(1, 2, 3));
    }
}

TEST(PaintTest, ADecalStandsUprightOnAWall) {
    const glm::quat rotation = editor::SurfaceRotation(glm::vec3(0, 0, 1), 0.0f);
    ExpectNear(rotation * glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    ExpectNear(rotation * glm::vec3(1, 0, 0), glm::vec3(1, 0, 0));
}

TEST(PaintTest, AWetPatchStandsOnTheSurfaceAndReachesBelow) {
    PaintBrush brush{.kind = PaintBrush::Kind::Wet, .size = 2.0f};
    const auto stamp = editor::Stamp(brush, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), 1.3f);
    ASSERT_TRUE(stamp.wet.has_value());
    EXPECT_FALSE(stamp.decal.has_value());
    ExpectNear(stamp.transform.rotation * glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    const float bottom = stamp.transform.translation.y - stamp.transform.scale.y * 0.5f;
    EXPECT_LT(bottom, 1.0f);
    EXPECT_GT(bottom, 0.8f);
    EXPECT_GT(stamp.wet->puddleHeight, 1.0f - bottom) << "Water stands above the floor";
    EXPECT_FLOAT_EQ(stamp.transform.scale.x, 2.0f);
}

TEST(PaintTest, AStrokeStampsEverySpacing) {
    EXPECT_FALSE(editor::NextStamp(glm::vec3(0), glm::vec3(0.3f, 0, 0), 0.5f).has_value());
    const auto next = editor::NextStamp(glm::vec3(0), glm::vec3(2, 0, 0), 0.5f);
    ASSERT_TRUE(next.has_value());
    ExpectNear(*next, glm::vec3(0.5f, 0, 0));
}

} // namespace

TEST(PaintTest, PropsStandOnTheSurfaceOrTurnToIt) {
    editor::PaintBrush brush;
    brush.kind = editor::PaintBrush::Kind::Props;
    brush.models = {"Props/can.gltf", "Props/bottle.gltf"};
    const glm::vec3 wall(1.0f, 0.0f, 0.0f);
    const daidalos::scene::LevelEntity upright = editor::Stamp(brush, glm::vec3(2.0f, 0.0f, 0.0f), wall, 0.0f, 1.5f, 3);
    EXPECT_EQ(upright.model, "Props/bottle.gltf");
    EXPECT_EQ(upright.transform.translation, glm::vec3(2.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(upright.transform.scale.y, 1.5f);
    // Upright, its +Y stays up whatever the surface.
    EXPECT_NEAR(glm::dot(upright.transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 1.0f,
                1e-5f);
    brush.alignToSurface = true;
    const daidalos::scene::LevelEntity turned = editor::Stamp(brush, glm::vec3(0.0f), wall, 0.7f);
    EXPECT_NEAR(glm::dot(turned.transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f), wall), 1.0f, 1e-5f);
}
