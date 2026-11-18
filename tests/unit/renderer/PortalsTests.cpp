// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Portals.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <vector>

using namespace daidalos;
using renderer::VisibilityCell;

namespace {

// Two rooms along -Z, 10 m long, joined by a 2 m doorway through a wall at z = -10; a third room past the
// second, joined at z = -20 by a doorway at the far side (x from 3 to 5).
const std::vector<VisibilityCell> kCells{
    {{-5.0f, 0.0f, -10.0f}, {5.0f, 3.0f, 0.0f}},   // 0: first room.
    {{-1.0f, 0.0f, -11.0f}, {1.0f, 2.5f, -9.0f}},  // 1: doorway between 0 and 2.
    {{-5.0f, 0.0f, -20.0f}, {5.0f, 3.0f, -10.0f}}, // 2: second room.
    {{3.0f, 0.0f, -21.0f}, {5.0f, 2.5f, -19.0f}},  // 3: doorway between 2 and 4.
    {{-5.0f, 0.0f, -30.0f}, {5.0f, 3.0f, -20.0f}}, // 4: third room.
};

glm::mat4 Looking(const glm::vec3& eye, const glm::vec3& target) {
    return glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 200.0f) *
           glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(PortalsTest, OutsideEveryCellEverythingMayBeSeen) {
    const glm::vec3 eye(0.0f, 1.6f, 20.0f);
    const std::vector<bool> seen = renderer::VisibleCells(kCells, Looking(eye, {0, 1.6f, 0}), eye);
    EXPECT_EQ(seen, std::vector<bool>(kCells.size(), true));
}

TEST(PortalsTest, ARoomIsSeenThroughItsDoorway) {
    const glm::vec3 eye(0.0f, 1.6f, -2.0f);
    const std::vector<bool> seen = renderer::VisibleCells(kCells, Looking(eye, {0, 1.6f, -10}), eye);
    EXPECT_TRUE(seen[0]);
    EXPECT_TRUE(seen[1]);
    EXPECT_TRUE(seen[2]) << "Through the doorway ahead";
}

TEST(PortalsTest, ARoomBehindIsNotSeen) {
    const glm::vec3 eye(0.0f, 1.6f, -2.0f);
    const std::vector<bool> seen = renderer::VisibleCells(kCells, Looking(eye, {0, 1.6f, 5}), eye);
    EXPECT_TRUE(seen[0]);
    EXPECT_FALSE(seen[2]) << "The doorway is behind the camera";
    EXPECT_FALSE(seen[4]);
}

TEST(PortalsTest, ADoorwayOutOfLineHidesTheRoomBeyond) {
    // From the far right of the first room, every line through the first doorway (x from -1 to 1) crosses
    // z = -20 left of x = -3: the far doorway (x from 3 to 5) is out of line.
    const glm::vec3 eye(4.5f, 1.6f, -1.0f);
    const std::vector<bool> seen = renderer::VisibleCells(kCells, Looking(eye, {4.5f, 1.6f, -30}), eye);
    EXPECT_TRUE(seen[0]);
    EXPECT_TRUE(seen[2]);
    EXPECT_FALSE(seen[4]) << "Not through both doorways";
    // From x = -2, lines through the first doorway reach x = 0.2 to 4.5 there: both doorways line up.
    const glm::vec3 lined(-2.0f, 1.6f, -1.0f);
    const std::vector<bool> through = renderer::VisibleCells(kCells, Looking(lined, {3.5f, 1.4f, -20}), lined);
    EXPECT_TRUE(through[2]);
    EXPECT_TRUE(through[4]);
}

TEST(PortalsTest, ADrawIsSeenInASeenCellOrOutsideTheCells) {
    const std::vector<bool> seen{true, false, false, false, false};
    EXPECT_TRUE(renderer::MayBeSeen(kCells, seen, {0, 1, -5}, 0.5f));
    EXPECT_FALSE(renderer::MayBeSeen(kCells, seen, {0, 1, -25}, 0.5f));
    EXPECT_TRUE(renderer::MayBeSeen(kCells, seen, {0, 1, 50}, 0.5f)) << "Outside every cell";
    EXPECT_TRUE(renderer::MayBeSeen(kCells, seen, {0, 1, -11}, 1.5f)) << "Across the wall, into the first room";
}

} // namespace
