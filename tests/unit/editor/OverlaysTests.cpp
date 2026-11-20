// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

#include "Overlays.hpp"

using namespace daidalos;
using namespace editor;

namespace {

/// A picture of a wall 5 m in front of a camera at the origin looking along -Z, over its left half only.
renderer::DepthPicture HalfWall() {
    renderer::DepthPicture picture;
    picture.width = 8;
    picture.height = 4;
    picture.meters.assign(32, 1.0e9f);
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            picture.meters[y * 8 + x] = 5.0f;
        }
    }
    picture.viewProjection = glm::perspective(1.0f, 2.0f, 0.1f, 100.0f) *
                             glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    return picture;
}

} // namespace

TEST(OverlaysTest, PointsBehindTheWallAreFaint) {
    const renderer::DepthPicture picture = HalfWall();
    const DepthTest test(&picture);
    EXPECT_FLOAT_EQ(test.Visibility(glm::vec3(-1.0f, 0.0f, -10.0f)), DepthTest::kHidden);
    // In front of the wall, and on it.
    EXPECT_FLOAT_EQ(test.Visibility(glm::vec3(-1.0f, 0.0f, -3.0f)), 1.0f);
    EXPECT_FLOAT_EQ(test.Visibility(glm::vec3(-1.0f, 0.0f, -5.0f)), 1.0f);
    // Where the sky shows, and behind the camera.
    EXPECT_FLOAT_EQ(test.Visibility(glm::vec3(1.0f, 0.0f, -10.0f)), 1.0f);
    EXPECT_FLOAT_EQ(test.Visibility(glm::vec3(0.0f, 0.0f, 10.0f)), 1.0f);
}

TEST(OverlaysTest, WithoutAPictureEverythingShows) {
    EXPECT_FLOAT_EQ(DepthTest().Visibility(glm::vec3(0.0f, 0.0f, -50.0f)), 1.0f);
}

TEST(OverlaysTest, OpeningsAreWhereRoomsOverlap) {
    Bounds hall;
    hall.Add(glm::vec3(0.0f));
    hall.Add(glm::vec3(10.0f, 3.0f, 10.0f));
    Bounds doorway;
    doorway.Add(glm::vec3(9.5f, 0.0f, 4.0f));
    doorway.Add(glm::vec3(10.5f, 2.0f, 5.0f));
    Bounds faraway;
    faraway.Add(glm::vec3(50.0f));
    faraway.Add(glm::vec3(60.0f));
    const std::vector<Bounds> rooms{hall, doorway, faraway};
    const std::vector<Bounds> openings = Openings(rooms);
    ASSERT_EQ(openings.size(), 1U);
    EXPECT_FLOAT_EQ(openings[0].min.x, 9.5f);
    EXPECT_FLOAT_EQ(openings[0].max.x, 10.0f);
    EXPECT_FLOAT_EQ(openings[0].max.y, 2.0f);
}

TEST(OverlaysTest, EntitiesFallInTheirCategory) {
    scene::LevelEntity light;
    light.light = scene::Light{};
    EXPECT_EQ(CategoryOf(light), Category::Lights);
    scene::LevelEntity model;
    model.model = "crate.gltf";
    EXPECT_EQ(CategoryOf(model), Category::Models);
    scene::LevelEntity room;
    room.cell = scene::VisibilityCell{};
    EXPECT_EQ(CategoryOf(room), Category::Rooms);
    EXPECT_EQ(CategoryOf(scene::LevelEntity{}), Category::Other);
}

TEST(OverlaysTest, GameViewHidesEveryCategory) {
    ShowFlags show;
    EXPECT_TRUE(show.Shows(Category::Lights));
    show.categories[static_cast<std::size_t>(Category::Lights)] = false;
    EXPECT_FALSE(show.Shows(Category::Lights));
    EXPECT_TRUE(show.Shows(Category::Game));
    show.gameView = true;
    EXPECT_FALSE(show.Shows(Category::Game));
}

TEST(OverlaysTest, ShowFlagsComeFromAList) {
    ShowFlags show;
    const std::vector<std::string> unknown = ApplyShowFlags(show, "collision, no_grid,portals,,sparkles");
    EXPECT_TRUE(show.collision);
    EXPECT_TRUE(show.portals);
    EXPECT_FALSE(show.grid);
    EXPECT_FALSE(show.navigation);
    ASSERT_EQ(unknown.size(), 1U);
    EXPECT_EQ(unknown[0], "sparkles");
}
