// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <string_view>

namespace editor {

/// The viewport's camera: it flies with the right mouse button held, as in most level editors.
struct EditorCamera {
    glm::vec3 position{0.0f, 2.0f, 6.0f};
    /// Radians: yaw 0 looks along -Z and turns left as it grows; pitch looks up as it grows.
    float yaw = 0.0f;
    float pitch = -0.2f;
    /// Meters per second.
    float speed = 6.0f;
    float verticalFov = 1.0471976f;
    float nearPlane = 0.05f;
    float farPlane = 2000.0f;

    [[nodiscard]] glm::mat4 World() const;
    [[nodiscard]] glm::mat4 View() const;
    [[nodiscard]] glm::vec3 Forward() const;
};

/// What a pane of the viewport looks from (ADR-0108).
enum class ViewKind { Perspective, Top, Front, Side };
[[nodiscard]] std::string_view ViewName(ViewKind kind);

/// A view along an axis, which the renderer draws as nearly orthographic: from far back, with a narrow lens. The
/// near plane cuts the level cut meters in front of the focus, so that the top view sees into rooms under their
/// ceilings, and the front and side views into rooms behind their walls.
struct OrthoView {
    /// The middle of the view; the cut is measured from it along the view's axis.
    glm::vec3 focus{0.0f};
    /// Meters from the middle of the view to its top.
    float halfHeight = 12.0f;
    /// Meters in front of the focus that still show.
    float cut = 0.5f;
};

/// How far back the camera of an axis view stands: far enough that a 10 m room is within 3% of the same scale
/// front to back, near enough that depth and ray offsets keep their precision.
inline constexpr float kAxisDistance = 400.0f;
inline constexpr float kMinHalfHeight = 0.5f;
inline constexpr float kMaxHalfHeight = 500.0f;
inline constexpr float kMinCut = -50.0f;
inline constexpr float kMaxCut = kAxisDistance - 1.0f;

/// The direction an axis view looks along, and its right and up on screen. Top looks down with -Z up; front looks
/// along -Z; side along -X.
[[nodiscard]] glm::vec3 ViewAxis(ViewKind kind);
[[nodiscard]] glm::vec3 ViewRight(ViewKind kind);
[[nodiscard]] glm::vec3 ViewUp(ViewKind kind);

/// The camera an axis view is drawn from.
[[nodiscard]] EditorCamera AxisEye(ViewKind kind, const OrthoView& view);

/// The focus moved by a drag of the mouse across a pane height pixels tall, so the level follows the mouse.
[[nodiscard]] glm::vec3 Panned(ViewKind kind, const OrthoView& view, const glm::vec2& pixels, float height);
/// The half height after turns of the wheel: in when positive.
[[nodiscard]] float Zoomed(float halfHeight, float turns);

/// The view along an axis that frames a box from low to high, cutting just in front of it.
[[nodiscard]] OrthoView Framed(ViewKind kind, const glm::vec3& low, const glm::vec3& high, float aspect);

/// A rectangle of the window in interface coordinates.
struct Rect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    [[nodiscard]] bool Contains(const glm::vec2& point) const {
        return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
    [[nodiscard]] glm::vec2 Size() const { return max - min; }
};

/// The four panes of the split layout in a rectangle, two by two, gap pixels apart: top left, top right, bottom
/// left, bottom right.
[[nodiscard]] std::array<Rect, 4> SplitPanes(const Rect& area, float gap);

} // namespace editor
