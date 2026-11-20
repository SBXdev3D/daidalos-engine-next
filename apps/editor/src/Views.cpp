// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Views.hpp"

#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

using namespace daidalos;

glm::mat4 EditorCamera::World() const {
    math::Transform transform;
    transform.translation = position;
    transform.rotation = math::FromYawPitchRoll(yaw, pitch, 0.0f);
    return math::ToMatrix(transform);
}

glm::mat4 EditorCamera::View() const {
    return glm::inverse(World());
}

glm::vec3 EditorCamera::Forward() const {
    return math::FromYawPitchRoll(yaw, pitch, 0.0f) * math::kForward;
}

std::string_view ViewName(ViewKind kind) {
    switch (kind) {
    case ViewKind::Perspective:
        return "Perspective";
    case ViewKind::Top:
        return "Top";
    case ViewKind::Front:
        return "Front";
    case ViewKind::Side:
        return "Side";
    }
    return "Perspective";
}

glm::vec3 ViewAxis(ViewKind kind) {
    switch (kind) {
    case ViewKind::Top:
        return {0.0f, -1.0f, 0.0f};
    case ViewKind::Side:
        return {-1.0f, 0.0f, 0.0f};
    case ViewKind::Front:
    case ViewKind::Perspective:
        break;
    }
    return {0.0f, 0.0f, -1.0f};
}

glm::vec3 ViewRight(ViewKind kind) {
    switch (kind) {
    case ViewKind::Side:
        return {0.0f, 0.0f, -1.0f};
    case ViewKind::Top:
    case ViewKind::Front:
    case ViewKind::Perspective:
        break;
    }
    return {1.0f, 0.0f, 0.0f};
}

glm::vec3 ViewUp(ViewKind kind) {
    return kind == ViewKind::Top ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

EditorCamera AxisEye(ViewKind kind, const OrthoView& view) {
    EditorCamera eye;
    eye.position = view.focus - ViewAxis(kind) * kAxisDistance;
    // Yaw 0 looks along -Z and turns left as it grows: -X is a quarter turn. Top looks straight down, north up.
    eye.yaw = kind == ViewKind::Side ? glm::half_pi<float>() : 0.0f;
    eye.pitch = kind == ViewKind::Top ? -glm::half_pi<float>() : 0.0f;
    eye.verticalFov = 2.0f * std::atan(std::clamp(view.halfHeight, kMinHalfHeight, kMaxHalfHeight) / kAxisDistance);
    eye.nearPlane = kAxisDistance - std::clamp(view.cut, kMinCut, kMaxCut);
    eye.farPlane = kAxisDistance * 2.0f;
    return eye;
}

glm::vec3 Panned(ViewKind kind, const OrthoView& view, const glm::vec2& pixels, float height) {
    const float metersPerPixel = 2.0f * view.halfHeight / std::max(height, 1.0f);
    return view.focus - ViewRight(kind) * pixels.x * metersPerPixel + ViewUp(kind) * pixels.y * metersPerPixel;
}

float Zoomed(float halfHeight, float turns) {
    return std::clamp(halfHeight * std::pow(1.15f, -turns), kMinHalfHeight, kMaxHalfHeight);
}

OrthoView Framed(ViewKind kind, const glm::vec3& low, const glm::vec3& high, float aspect) {
    const glm::vec3 center = (low + high) * 0.5f;
    const glm::vec3 half = (high - low) * 0.5f;
    const glm::vec3 axis = ViewAxis(kind);
    OrthoView view;
    // The focus on the side of the box the camera is on, so that the cut in front of it leaves the box whole.
    view.focus = center - axis * glm::dot(half, glm::abs(axis));
    const float across = glm::dot(half, glm::abs(ViewRight(kind)));
    const float up = glm::dot(half, glm::abs(ViewUp(kind)));
    view.halfHeight =
        std::clamp(std::max(up, across / std::max(aspect, 0.1f)) * 1.25f + 0.5f, kMinHalfHeight, kMaxHalfHeight);
    view.cut = 0.5f;
    return view;
}

std::array<Rect, 4> SplitPanes(const Rect& area, float gap) {
    const glm::vec2 middle = (area.min + area.max) * 0.5f;
    const float half = gap * 0.5f;
    return {Rect{area.min, {middle.x - half, middle.y - half}},
            Rect{{middle.x + half, area.min.y}, {area.max.x, middle.y - half}},
            Rect{{area.min.x, middle.y + half}, {middle.x - half, area.max.y}},
            Rect{{middle.x + half, middle.y + half}, area.max}};
}

} // namespace editor
