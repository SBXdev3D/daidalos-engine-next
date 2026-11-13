// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/CornellBox.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <string>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr float kSize = 5.5f;
constexpr float kHalf = kSize * 0.5f;
constexpr float kWall = 0.1f;

MaterialDesc Matte(const glm::vec3& color) {
    MaterialDesc material;
    material.baseColorFactor = glm::vec4(color, 1.0f);
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    return material;
}

CornellPart Box(std::string name, const glm::vec3& center, const glm::vec3& size, float yawDegrees,
                const glm::vec3& color) {
    const float yaw = glm::radians(yawDegrees);
    glm::mat4 world = glm::translate(glm::mat4(1.0f), center);
    world = glm::rotate(world, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    world = glm::scale(world, size);
    return {std::move(name), center, size, yaw, world, Matte(color)};
}

} // namespace

CornellBox MakeCornellBox() {
    // The reflectances of the original measurements, roughly.
    const glm::vec3 white(0.73f);
    const glm::vec3 red(0.63f, 0.065f, 0.05f);
    const glm::vec3 green(0.14f, 0.45f, 0.091f);
    const float outer = kSize + 2.0f * kWall;

    CornellBox box;
    box.parts = {
        Box("Floor", {0.0f, -kWall * 0.5f, 0.0f}, {outer, kWall, outer}, 0.0f, white),
        Box("Ceiling", {0.0f, kSize + kWall * 0.5f, 0.0f}, {outer, kWall, outer}, 0.0f, white),
        Box("Back wall", {0.0f, kHalf, -kHalf - kWall * 0.5f}, {outer, kSize, kWall}, 0.0f, white),
        Box("Left wall", {-kHalf - kWall * 0.5f, kHalf, 0.0f}, {kWall, kSize, kSize}, 0.0f, red),
        Box("Right wall", {kHalf + kWall * 0.5f, kHalf, 0.0f}, {kWall, kSize, kSize}, 0.0f, green),
        Box("Tall block", {-0.95f, 1.65f, -0.75f}, {1.65f, 3.3f, 1.65f}, 17.0f, white),
        Box("Short block", {0.95f, 0.825f, 0.75f}, {1.65f, 1.65f, 1.65f}, -17.0f, white),
    };
    // A spot shining down, as the panel of the original does: a light that also shone up would make, 25 cm
    // from the ceiling, a small patch much brighter than the rest of the room, which one ray per pixel finds
    // too rarely for its bounce to be smooth. 500 candela: at EV100 3, the image is about as bright as
    // before physical units (ADR-0043).
    box.light = {.type = LightType::Spot,
                 .position = {0.0f, kSize - 0.25f, 0.0f},
                 .direction = {0.0f, -1.0f, 0.0f},
                 .color = glm::vec3(500.0f),
                 .range = 12.0f,
                 .innerAngle = glm::radians(60.0f),
                 .outerAngle = glm::radians(85.0f)};
    box.eye = {0.0f, kHalf, 10.0f};
    box.target = {0.0f, kHalf, 0.0f};
    box.verticalFov = glm::radians(42.0f);
    return box;
}

} // namespace daidalos::renderer
