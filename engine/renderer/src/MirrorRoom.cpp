// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/MirrorRoom.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <string>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr float kWidth = 6.0f;
constexpr float kHalf = kWidth * 0.5f;
constexpr float kHeight = 3.0f;
constexpr float kWall = 0.1f;

MaterialDesc Surface(const glm::vec3& color, float roughness, float metallic) {
    MaterialDesc material;
    material.baseColorFactor = glm::vec4(color, 1.0f);
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
    return material;
}

MaterialDesc Glass(const glm::vec3& tint, bool thin, const glm::vec3& attenuation, float distance) {
    MaterialDesc material = Surface(tint, 0.0f, 0.0f);
    material.transmission = 1.0f;
    material.ior = 1.5f;
    material.thin = thin;
    material.attenuationColor = attenuation;
    material.attenuationDistance = distance;
    material.doubleSided = thin;
    return material;
}

CornellPart Box(std::string name, const glm::vec3& center, const glm::vec3& size, float yawDegrees,
                const MaterialDesc& material) {
    const float yaw = glm::radians(yawDegrees);
    glm::mat4 world = glm::translate(glm::mat4(1.0f), center);
    world = glm::rotate(world, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    world = glm::scale(world, size);
    return {std::move(name), center, size, yaw, world, material};
}

} // namespace

MirrorRoom MakeMirrorRoom() {
    const MaterialDesc white = Surface(glm::vec3(0.7f), 1.0f, 0.0f);
    const float outer = kWidth + 2.0f * kWall;

    MirrorRoom room;
    room.parts = {
        Box("Floor", {0.0f, -kWall * 0.5f, 0.0f}, {outer, kWall, outer}, 0.0f, Surface(glm::vec3(0.35f), 0.2f, 0.0f)),
        Box("Ceiling", {0.0f, kHeight + kWall * 0.5f, 0.0f}, {outer, kWall, outer}, 0.0f, white),
        Box("Back wall", {0.0f, kHeight * 0.5f, -kHalf - kWall * 0.5f}, {outer, kHeight, kWall}, 0.0f, white),
        Box("Left wall", {-kHalf - kWall * 0.5f, kHeight * 0.5f, 0.0f}, {kWall, kHeight, kWidth}, 0.0f,
            Surface({0.6f, 0.12f, 0.08f}, 1.0f, 0.0f)),
        Box("Right wall", {kHalf + kWall * 0.5f, kHeight * 0.5f, 0.0f}, {kWall, kHeight, kWidth}, 0.0f,
            Surface({0.15f, 0.3f, 0.55f}, 1.0f, 0.0f)),
        // A silver mirror a few centimeters in front of the back wall.
        Box("Mirror", {0.0f, 1.5f, -kHalf + 0.02f}, {4.0f, 2.2f, 0.02f}, 0.0f, Surface(glm::vec3(0.95f), 0.05f, 1.0f)),
        Box("Gold block", {-1.6f, 0.5f, -1.0f}, {1.0f, 1.0f, 1.0f}, 25.0f, Surface({1.0f, 0.78f, 0.34f}, 0.3f, 1.0f)),
        Box("White block", {1.5f, 0.6f, -0.8f}, {1.0f, 1.2f, 1.0f}, -15.0f, white),
        Box("Window pane", {1.2f, 0.75f, 0.6f}, {1.4f, 1.5f, 0.01f}, -10.0f,
            Glass(glm::vec3(0.95f, 1.0f, 0.97f), true, glm::vec3(1.0f), 0.0f)),
        Box("Glass block", {-0.3f, 0.4f, 0.9f}, {0.8f, 0.8f, 0.8f}, 35.0f,
            Glass(glm::vec3(1.0f), false, {0.35f, 0.8f, 0.7f}, 0.5f)),
    };
    const Light lamp{.type = LightType::Point,
                     .position = {0.0f, kHeight - 0.3f, 0.0f},
                     .color = glm::vec3(1.0f, 0.92f, 0.8f),
                     .range = 12.0f,
                     .falloff = 0.0f};
    room.lights = {lamp, lamp};
    room.lights[0].position = {-1.8f, kHeight - 0.3f, 1.2f};
    room.lights[1].position = {1.8f, kHeight - 0.3f, -1.2f};
    room.lights[1].color = glm::vec3(0.7f, 0.75f, 0.9f);
    room.eye = {0.6f, 1.6f, 7.0f};
    room.target = {0.0f, 1.0f, 0.0f};
    room.verticalFov = glm::radians(45.0f);
    return room;
}

} // namespace daidalos::renderer
