// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/DarkCorridor.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <string>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr float kWall = 0.1f;
constexpr float kHeight = 3.0f;
constexpr float kLength = 20.0f;
// The doorway, on the right wall, and the side room behind it.
constexpr float kDoorNear = -8.0f;
constexpr float kDoorFar = -9.2f;
constexpr float kDoorHeight = 2.2f;
constexpr float kRoomNear = -6.0f;
constexpr float kRoomFar = -11.0f;
constexpr float kRoomRight = 5.0f;

// The flashlight of the sandbox's player (apps/sandbox/src/Factory.cpp).
constexpr glm::vec3 kFlashlightColor{1.06f, 1.0f, 0.86f};
constexpr float kFlashlightCandela = 1090.0f;
constexpr float kFlashlightRange = 32.0f;
constexpr glm::vec3 kFlashlightHand{-0.3f, -0.15f, 0.0f};
constexpr float kFlashlightAimDistance = 8.0f;

MaterialDesc Matte(const glm::vec3& color) {
    MaterialDesc material;
    material.baseColorFactor = glm::vec4(color, 1.0f);
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    return material;
}

// A box between two corners, turned around the vertical through its center.
CornellPart Slab(std::string name, const glm::vec3& from, const glm::vec3& to, const glm::vec3& color,
                 float yawDegrees = 0.0f) {
    const glm::vec3 center = (from + to) * 0.5f;
    const glm::vec3 size = glm::abs(to - from);
    const float yaw = glm::radians(yawDegrees);
    glm::mat4 world = glm::translate(glm::mat4(1.0f), center);
    world = glm::rotate(world, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    world = glm::scale(world, size);
    return {std::move(name), center, size, yaw, world, Matte(color)};
}

} // namespace

DarkCorridor MakeDarkCorridor() {
    const glm::vec3 grey(0.5f);
    const glm::vec3 floor(0.35f);
    const glm::vec3 rust(0.55f, 0.3f, 0.15f);
    const float near = kWall;
    const float far = -kLength - kWall;

    DarkCorridor corridor;
    // The walls go into each other, so that no ray finds a seam between them.
    corridor.parts = {
        Slab("Floor", {-1.0f - kWall, -kWall, near}, {1.0f + kWall, 0.0f, far}, floor),
        Slab("Ceiling", {-1.0f - kWall, kHeight, near}, {1.0f + kWall, kHeight + kWall, far}, grey),
        Slab("Left wall", {-1.0f - kWall, 0.0f, near}, {-1.0f, kHeight, far}, grey),
        Slab("Start wall", {-1.0f, 0.0f, near}, {1.0f, kHeight, 0.0f}, grey),
        Slab("End wall", {-1.0f, 0.0f, -kLength}, {1.0f, kHeight, far}, grey),
        // The right wall, around the doorway.
        Slab("Right wall, near", {1.0f, 0.0f, near}, {1.0f + kWall, kHeight, kDoorNear}, grey),
        Slab("Right wall, far", {1.0f, 0.0f, kDoorFar}, {1.0f + kWall, kHeight, far}, grey),
        Slab("Lintel", {1.0f, kDoorHeight, kDoorNear}, {1.0f + kWall, kHeight, kDoorFar}, grey),
        // The side room.
        Slab("Room floor", {1.0f, -kWall, kRoomNear + kWall}, {kRoomRight + kWall, 0.0f, kRoomFar - kWall}, floor),
        Slab("Room ceiling", {1.0f, kHeight, kRoomNear + kWall},
             {kRoomRight + kWall, kHeight + kWall, kRoomFar - kWall}, grey),
        Slab("Room near wall", {1.0f + kWall, 0.0f, kRoomNear + kWall}, {kRoomRight + kWall, kHeight, kRoomNear}, grey),
        Slab("Room far wall", {1.0f + kWall, 0.0f, kRoomFar}, {kRoomRight + kWall, kHeight, kRoomFar - kWall}, grey),
        Slab("Room back wall", {kRoomRight, 0.0f, kRoomNear}, {kRoomRight + kWall, kHeight, kRoomFar}, grey),
        // Boxes: along the left wall, one rusty, and a tall one farther on.
        Slab("Box", {-0.95f, 0.0f, -4.6f}, {-0.35f, 0.6f, -5.2f}, grey, 10.0f),
        Slab("Rusty box", {0.2f, 0.0f, -6.2f}, {0.8f, 0.5f, -6.8f}, rust, -20.0f),
        Slab("Tall box", {-0.9f, 0.0f, -12.0f}, {-0.3f, 1.4f, -12.6f}, grey, 5.0f),
        Slab("Room box", {3.4f, 0.0f, -9.6f}, {4.2f, 0.8f, -10.4f}, rust, 30.0f),
    };
    corridor.eye = {0.2f, 1.6f, -1.0f};
    corridor.target = {0.0f, 1.2f, -9.0f};
    corridor.verticalFov = glm::radians(60.0f);
    return corridor;
}

Light Flashlight(const glm::vec3& eye, const glm::vec3& target) {
    const glm::vec3 forward = glm::normalize(target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    const glm::vec3 hand = eye + right * kFlashlightHand.x + up * kFlashlightHand.y - forward * kFlashlightHand.z;
    return {.type = LightType::Spot,
            .position = hand,
            .direction = glm::normalize(eye + forward * kFlashlightAimDistance - hand),
            .color = kFlashlightColor * kFlashlightCandela,
            .range = kFlashlightRange,
            .innerAngle = glm::radians(13.0f),
            .outerAngle = glm::radians(22.0f)};
}

} // namespace daidalos::renderer
