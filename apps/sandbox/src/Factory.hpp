// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/input/ActionMap.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

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
    /// Pauses the game, or goes back from a menu.
    Menu,
    Fire,
    /// The gamepad's trigger, which fires past half of its travel.
    FireTrigger,
    Reload,
    NextWeapon,
    SelectHandgun,
    SelectShotgun,
};

enum class WeaponKind : std::uint8_t { Handgun, Shotgun };
inline constexpr std::size_t kWeaponKinds = 2;

/// Rounds in the weapon, and the ones the player carries for it.
struct WeaponState {
    bool owned = false;
    int magazine = 0;
    int reserve = 0;
};

[[nodiscard]] daidalos::input::ActionMap MakeActions();

/// The particle effects that the game's code spawns, loaded from their files with the level's assets
/// (ADR-0053), as a component of the camera entity. The level's emitters name their own files.
struct ParticleEffects {
    daidalos::renderer::ParticleEffectHandle blood;
    daidalos::renderer::ParticleEffectHandle sparks;
    daidalos::renderer::ParticleEffectHandle gunSmoke;
    daidalos::renderer::ParticleEffectHandle muzzleFlash;
    /// Thrown out of the weapon's side at each shot (ADR-0053).
    daidalos::renderer::ParticleEffectHandle casings;
    /// Where a shot lands, by what it hits (ADR-0053): dust, and chips of stone, splinters of wood, glass.
    daidalos::renderer::ParticleEffectHandle impactDust;
    daidalos::renderer::ParticleEffectHandle impactChips;
    daidalos::renderer::ParticleEffectHandle impactSplinters;
    daidalos::renderer::ParticleEffectHandle impactGlass;
};

/// What the level's bodies are made of, by body, from their materials' surface (ADR-0053): "concrete",
/// "metal", "wood", "glass". A body that is not here is of no known surface. A component of the camera entity.
struct BodySurfaces {
    std::unordered_map<std::uint32_t, std::string> surfaces;

    [[nodiscard]] std::string_view Of(daidalos::physics::BodyId body) const {
        const auto found = surfaces.find(body.value);
        return found == surfaces.end() ? std::string_view{} : std::string_view(found->second);
    }
};

/// The player's state, as a component of the camera entity.
struct Player {
    daidalos::physics::CharacterHandle character;
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
    /// The handgun comes loaded, with nothing to reload it.
    std::array<WeaponState, kWeaponKinds> weapons{{{true, 12, 0}, {}}};
    WeaponKind weapon = WeaponKind::Handgun;
    /// Seconds before the weapon can fire again, or finishes its reload or its switch.
    float weaponBusy = 0.0f;
    bool reloading = false;
    /// Seconds into the switch to the current weapon: the old one goes down, the new one comes up.
    float switching = 0.0f;
    bool dead = false;
    /// Seconds left of the red flash of a wound.
    float hurtTime = 0.0f;
    int kills = 0;
    int zombieCount = 0;
    bool hasFlashlight = false;
    bool flashlightOn = false;
    /// Lights that follow the camera, as its children.
    daidalos::ecs::Entity flashlight;
    daidalos::ecs::Entity muzzleFlash;
    /// Seconds left of the light of the last shot.
    float muzzleFlashTime = 0.0f;
    bool hasRedCard = false;
    /// Last thing that happened, and how long it stays on screen, in seconds.
    std::string message;
    float messageTime = 0.0f;
};

/// Settings of the game, which must outlive the scene.
struct Settings {
    float mouseSensitivity = 0.0025f;
    /// 1 is the factory's darkness, and 0 shows it much brighter, without its haze.
    float darkness = 1.0f;
    /// Starts with the flashlight on, for tests, instead of finding it in the east wing.
    bool startWithFlashlight = false;
};

/// A sound that does not stop the game when it is missing, because the game can be played without it.
/// The name is a file of Factory/Sounds.
[[nodiscard]] daidalos::audio::ClipHandle LoadSound(daidalos::scene::Scene& scene, std::string_view name,
                                                    const daidalos::audio::ClipOptions& options = {});

/// Places the player's eyes, for example from sandbox.start.
void Teleport(daidalos::scene::Scene& scene, daidalos::ecs::Entity entity, const glm::vec3& eye);

/// Creates the entities of a level: the player, the doors, the ladders and the pickups are found by
/// the names of the model's nodes, and the "level" node is what the player walks on. Nodes with a
/// "model" attribute get that model, from the level's directory: zombies and fans. Returns the
/// player's entity.
[[nodiscard]] daidalos::core::Result<daidalos::ecs::Entity>
BuildLevel(daidalos::scene::Scene& scene, const daidalos::scene::Model& model, std::string_view levelPath);

/// Puts the effects of mesh particles and ribbons in front of a place (ADR-0053), for sandbox.showcase: an
/// electric arc, a laser, lightning, breaking glass, papers in the air and a wisp that circles. Their files
/// are in the level's directory. yaw is the way the place looks, as sandbox.start gives it, in radians.
void AddEffectShowcase(daidalos::scene::Scene& scene, std::string_view levelPath, const glm::vec3& eye, float yaw);

/// What the input does to the world. The input and the actions must outlive the scene.
void AddSystems(daidalos::scene::Scene& scene, const daidalos::input::InputState& input,
                const daidalos::input::ActionMap& actions, const Settings& settings);

} // namespace sandbox
