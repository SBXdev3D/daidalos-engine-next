// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/input/ActionMap.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/AssetLibrary.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/vec3.hpp>

#include <string>

namespace sandbox {

// The demo game, on the level of the original Daidalos demo (docs/DEMO.md). What is here belongs to
// this game, not to the engine.

enum Action : daidalos::input::Action {
    MoveForward,
    MoveRight,
    Jump,
    Crouch,
    Run,
    Interact,
    ToggleFlashlight,
    Restart,
    ToggleMouse,
    Quit,
};

[[nodiscard]] daidalos::input::ActionMap MakeActions();

/// The player's state, as a component of the camera entity.
struct Player {
    daidalos::physics::CharacterId character;
    /// Where the feet were after the last two fixed steps: the camera draws in between.
    glm::vec3 previousFeet{0.0f};
    glm::vec3 feet{0.0f};
    float verticalSpeed = 0.0f;
    float eyeHeight = 1.5f;
    bool crouching = false;
    /// Presses last one frame, and fixed steps do not run on every frame: they wait here.
    bool jumpPressed = false;
    bool interactPressed = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int health = 80;
    int armor = 0;
    int handgunAmmo = 12;
    int shotgunAmmo = 0;
    bool hasFlashlight = false;
    bool flashlightOn = false;
    bool hasRedCard = false;
    /// Last thing that happened, and how long it stays on screen, in seconds.
    std::string message;
    float messageTime = 0.0f;
};

/// Settings of the game, which must outlive the scene.
struct Settings {
    float mouseSensitivity = 0.0025f;
    /// Multiplies the darkness; 0 shows the whole level.
    float darkness = 1.0f;
};

/// Places the player's eyes, for example from sandbox.start.
void Teleport(daidalos::scene::Scene& scene, daidalos::ecs::Entity entity, const glm::vec3& eye);

/// Creates the entities of a level: the player, the doors, the ladders and the pickups are found by
/// the names of the model's nodes, and the "level" node is what the player walks on. Returns the
/// player's entity.
[[nodiscard]] daidalos::core::Result<daidalos::ecs::Entity> BuildLevel(daidalos::scene::Scene& scene,
                                                                       const daidalos::renderer::Model& model);

/// What the input does to the world. The input and the actions must outlive the scene.
void AddSystems(daidalos::scene::Scene& scene, const daidalos::input::InputState& input,
                const daidalos::input::ActionMap& actions, const Settings& settings);

} // namespace sandbox
