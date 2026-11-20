// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include "Views.hpp"

using namespace editor;

namespace {

/// Where a point lands on a view's screen, from -1 to 1 across and up.
glm::vec2 OnScreen(const EditorCamera& eye, float aspect, const glm::vec3& point) {
    const glm::vec4 clip =
        glm::perspective(eye.verticalFov, aspect, eye.nearPlane, eye.farPlane) * eye.View() * glm::vec4(point, 1.0f);
    return glm::vec2(clip) / clip.w;
}

} // namespace

TEST(ViewsTest, AxisViewsLookAlongTheirAxis) {
    for (const ViewKind kind : {ViewKind::Top, ViewKind::Front, ViewKind::Side}) {
        const EditorCamera eye = AxisEye(kind, OrthoView{.focus = glm::vec3(1.0f, 2.0f, 3.0f)});
        EXPECT_LT(glm::distance(eye.Forward(), ViewAxis(kind)), 1e-4f) << ViewName(kind);
        // Right and up on screen are the view's.
        const OrthoView view{.focus = glm::vec3(1.0f, 2.0f, 3.0f), .halfHeight = 10.0f};
        const glm::vec2 right = OnScreen(AxisEye(kind, view), 1.0f, view.focus + ViewRight(kind) * 5.0f);
        const glm::vec2 up = OnScreen(AxisEye(kind, view), 1.0f, view.focus + ViewUp(kind) * 5.0f);
        EXPECT_NEAR(right.x, 0.5f, 1e-3f) << ViewName(kind);
        EXPECT_NEAR(right.y, 0.0f, 1e-3f) << ViewName(kind);
        EXPECT_NEAR(up.y, 0.5f, 1e-3f) << ViewName(kind);
    }
}

TEST(ViewsTest, TheNearPlaneCutsInFrontOfTheFocus) {
    const OrthoView view{.focus = glm::vec3(0.0f, 1.5f, 0.0f), .cut = 0.5f};
    const EditorCamera eye = AxisEye(ViewKind::Top, view);
    // The ceiling at 3 m is in front of the near plane; the floor is behind it.
    EXPECT_NEAR(glm::distance(eye.position, view.focus) - eye.nearPlane, 0.5f, 1e-3f);
    EXPECT_GT(eye.farPlane, kAxisDistance + 100.0f);
}

TEST(ViewsTest, NearlyOrthographicOverARoom) {
    // A 10 m room seen from the side: its near and far walls' lengths on screen differ by under 3%.
    const OrthoView view{.focus = glm::vec3(0.0f), .halfHeight = 10.0f, .cut = 5.0f};
    const EditorCamera eye = AxisEye(ViewKind::Front, view);
    const float nearWall = OnScreen(eye, 1.0f, {4.0f, 0.0f, 5.0f}).x;
    const float farWall = OnScreen(eye, 1.0f, {4.0f, 0.0f, -5.0f}).x;
    EXPECT_LT(nearWall / farWall - 1.0f, 0.03f);
}

TEST(ViewsTest, PanningFollowsTheMouse) {
    const OrthoView view{.focus = glm::vec3(0.0f), .halfHeight = 10.0f};
    // Dragging 100 pixels right in a 200 pixel tall pane moves the level 10 m right: the focus goes left.
    const glm::vec3 panned = Panned(ViewKind::Top, view, glm::vec2(100.0f, 0.0f), 200.0f);
    EXPECT_NEAR(panned.x, -10.0f, 1e-4f);
    EXPECT_FLOAT_EQ(Zoomed(10.0f, 0.0f), 10.0f);
    EXPECT_LT(Zoomed(10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(Zoomed(10.0f, 1000.0f), kMinHalfHeight);
}

TEST(ViewsTest, FramingKeepsTheBoxInFrontOfTheCut) {
    const OrthoView view = Framed(ViewKind::Top, {-1.0f, 0.0f, -2.0f}, {1.0f, 3.0f, 2.0f}, 1.0f);
    EXPECT_FLOAT_EQ(view.focus.y, 3.0f);
    EXPECT_FLOAT_EQ(view.focus.x, 0.0f);
    EXPECT_GE(view.halfHeight, 2.0f);
}

TEST(ViewsTest, SplitPanesTileTheArea) {
    const std::array<Rect, 4> panes = SplitPanes(Rect{{0.0f, 0.0f}, {200.0f, 100.0f}}, 4.0f);
    EXPECT_EQ(panes[0].min, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(panes[0].max, glm::vec2(98.0f, 48.0f));
    EXPECT_EQ(panes[3].min, glm::vec2(102.0f, 52.0f));
    EXPECT_EQ(panes[3].max, glm::vec2(200.0f, 100.0f));
    EXPECT_TRUE(panes[1].Contains({150.0f, 10.0f}));
    EXPECT_FALSE(panes[1].Contains({50.0f, 10.0f}));
}
