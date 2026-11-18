// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Factory.hpp"

#include <daidalos/animation/Animation.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Combat.hpp"

using namespace daidalos;

namespace sandbox {

namespace {

// The numbers of the original demo, whose units were 0.35 m.
constexpr float kWalkSpeed = 3.5f; // Meters per second.
constexpr float kRunFactor = 1.7f;
constexpr float kCrouchFactor = 0.6f;
constexpr float kGravity = 7.0f;   // Meters per second squared.
constexpr float kJumpSpeed = 3.5f; // Meters per second.
constexpr float kClimbSpeed = 2.1f;
constexpr float kLadderReach = 0.5f;       // Meters around a ladder that the player can hold it from.
constexpr float kLadderTopMargin = 0.2f;   // Meters above a ladder that the player can climb to.
constexpr float kLookDownOnLadder = -0.4f; // Radians of pitch: below that, forward climbs down.
constexpr float kStandHeight = 1.7f;
constexpr float kCrouchHeight = 0.9f;
constexpr float kEyeHeight = 1.5f; // Above the floor.
constexpr float kCrouchEyeHeight = 0.75f;
constexpr float kEyeSpeed = 4.0f; // Meters per second, when crouching and standing up.
constexpr float kPickupRadius = 1.75f;
constexpr float kPickupSpin = glm::radians(70.0f); // Radians per second.
constexpr float kDoorRadius = 3.5f;                // Opens by itself this close.
constexpr float kDoorOpenTime = 6.0f;              // Seconds before it closes again.
constexpr float kDoorRise = 2.3f;                  // Meters.
constexpr float kDoorSpeed = 2.3f;                 // Meters per second.
constexpr int kMaxHealth = 100;
constexpr int kHealthBonus = 25;
constexpr int kArmorBonus = 50;
constexpr int kHandgunAmmoBonus = 12;
constexpr int kShotgunAmmoBonus = 7;
constexpr float kDeadEyeHeight = 0.3f; // Above the floor, once the player is down.
constexpr float kMaxPitch = glm::radians(89.0f);
constexpr float kStickLookSpeed = 2.5f; // Radians per second.
constexpr float kMessageTime = 3.0f;    // Seconds.

// The factory's exposure, fog and sky are its level file's (ADR-0054). Without its darkness, Settings::darkness 0,
// it is exposed this many stops brighter, without its haze.
constexpr float kBrightStops = 4.0f;
constexpr glm::vec3 kFlashlightColor{1.06f, 1.0f, 0.86f};
constexpr float kFlashlightCandela = 1090.0f; // As bright as before 4 m away.
constexpr float kFlashlightRange = 32.0f;     // Meters.
// The beam's shape is its profile's (ADR-0058): the cone only bounds it.
constexpr float kFlashlightInnerAngle = glm::radians(26.0f); // From the axis.
constexpr float kFlashlightOuterAngle = glm::radians(30.0f);
// The flashlight is in the left hand, the weapon in the right: from the eye, its shadows would all fall
// straight behind what casts them, out of sight. It aims at what the player looks at, a few meters ahead.
constexpr glm::vec3 kFlashlightHand{-0.3f, -0.15f, 0.0f};
constexpr float kFlashlightAimDistance = 8.0f;
// The light of a shot, after the legacy handgun's: 0.7 grey, 45 degrees, 0.7 m in front of the eye.
constexpr glm::vec3 kMuzzleFlashColor{1.11f, 1.0f, 0.72f};
constexpr float kMuzzleFlashCandela = 150.0f; // As bright as before 2 m away.
constexpr float kMuzzleFlashRange = 7.0f;
// Each alarm turns, with the two red spots its level file hangs under it.
constexpr float kAlarmSpeed = 70.0f; // Degrees per second, when the level does not say.

// Sounds. Distances are the steps of the original, and a fall is heard from about 0.7 m high.
constexpr float kStepDistance = 2.45f;   // Meters walked between two footsteps.
constexpr float kLadderDistance = 1.05f; // Meters climbed between two sounds.
constexpr float kLandingSpeed = 4.0f;    // Meters per second.
constexpr float kMusicVolume = 0.35f;

enum class PickupKind { Health, Armor, HandgunAmmo, ShotgunAmmo, Flashlight, RedCard, Shotgun };
constexpr std::size_t kPickupKinds = 7;

struct Pickup {
    PickupKind kind;
};

/// A sound that plays for as long as the level does, from where its entity is.
struct LoopedSound {
    audio::ClipHandle clip;
    float volume = 1.0f;
    float minDistance = 1.5f;
    float maxDistance = 35.0f;
};

/// The sounds of the game, with the player's entity.
struct SoundBank {
    std::array<audio::ClipHandle, 4> steps;
    /// Footsteps and impacts by what the surface is made of (ADR-0067): scripts/make_surface_sounds.py.
    std::map<std::string, std::vector<audio::ClipHandle>, std::less<>> surfaceSteps;
    std::map<std::string, std::vector<audio::ClipHandle>, std::less<>> surfaceImpacts;
    /// The mix the player's state asks for (ADR-0067): 0 normal, 1 hurt, 2 dead.
    int mixState = 0;
    std::array<audio::ClipHandle, kPickupKinds> pickups;
    audio::ClipHandle jump;
    audio::ClipHandle land;
    audio::ClipHandle ladder;
    audio::ClipHandle flashlight;
    audio::ClipHandle doorStart;
    audio::ClipHandle doorStop;
    audio::ClipHandle alarm;
    audio::ClipHandle fan;
    audio::ClipHandle music;
    float stepDistance = 0.0f;
    float ladderDistance = 0.0f;
    std::size_t lastStep = 0;
    std::minstd_rand random{std::random_device{}()};
};

/// The box around a ladder, in which the player climbs.
struct Ladder {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// A light fixture that turns around the vertical axis.
struct Alarm {
    float degreesPerSecond = kAlarmSpeed;
};

struct Door {
    int id = 0;
    glm::vec3 closedPosition{0.0f};
    /// Below 0, the door waits for the player to use it.
    float radius = kDoorRadius;
    bool closable = true;
    bool locked = false;
    bool open = false;
    /// Meters above the closed position.
    float rise = 0.0f;
    float timeLeft = 0.0f;
};

template <class T>
T Number(const scene::Tags& tags, std::string_view key, T fallback) {
    const std::string_view text = tags.Value(key);
    T value = fallback;
    if (!text.empty()) {
        std::from_chars(text.data(), text.data() + text.size(), value);
    }
    return value;
}

/// A value "x,y,z".
glm::vec3 Vector(const scene::Tags& tags, std::string_view key) {
    glm::vec3 value{0.0f};
    const std::string_view text = tags.Value(key);
    const char* cursor = text.data();
    const char* const end = cursor + text.size();
    for (int axis = 0; axis < 3 && cursor < end; ++axis) {
        cursor = std::from_chars(cursor, end, value[axis]).ptr + 1; // Past the comma.
    }
    return value;
}

/// A child entity that holds a light, placed and turned relative to its parent.
ecs::Entity AddLight(ecs::World& world, ecs::Entity parent, const glm::vec3& offset, const glm::quat& rotation,
                     const scene::Light& light) {
    const ecs::Entity entity = world.Create();
    ecs::LocalTransform transform;
    transform.translation = offset;
    transform.rotation = rotation;
    world.Add<ecs::LocalTransform>(entity, transform);
    world.Add<scene::Light>(entity, light);
    [[maybe_unused]] const core::Result<void> attached = world.SetParent(entity, parent);
    return entity;
}

/// Where the level's effect files are: the effects of the legacy game's particle templates, in meters and
/// seconds, in files since ADR-0053.
std::string EffectPath(const std::string& directory, std::string_view name) {
    return std::format("{}Effects/{}.effect.json", directory.empty() ? "" : directory + "/", name);
}

/// Where the level's ribbon files are (ADR-0053).
std::string RibbonPath(const std::string& directory, std::string_view name) {
    return std::format("{}Effects/{}.ribbon.json", directory.empty() ? "" : directory + "/", name);
}

/// A decal's image of the level's Decals folder (ADR-0060), or none when it is missing.
rhi::TextureHandle Decal(scene::Scene& scene, const std::string& directory, std::string_view name,
                         resources::TextureUse use) {
    const std::string path = std::format("{}Decals/{}", directory.empty() ? "" : directory + "/", name);
    core::Result<rhi::TextureHandle> texture = scene.Assets().LoadTexture(path, use);
    if (!texture) {
        DAI_LOG_WARNING("sandbox", "A decal is missing: {}", core::Format(texture.error()));
        return {};
    }
    return *texture;
}

ParticleEffects LoadParticleEffects(scene::Scene& scene, const std::string& directory) {
    const auto load = [&](std::string_view name) {
        core::Result<scene::ParticleEffectAsset> effect =
            scene.Assets().LoadParticleEffect(EffectPath(directory, name));
        if (!effect) {
            DAI_LOG_WARNING("sandbox", "A particle effect is missing: {}", core::Format(effect.error()));
            return renderer::ParticleEffectHandle{};
        }
        return effect->effect;
    };
    return {.blood = load("blood"),
            .sparks = load("sparks"),
            .gunSmoke = load("gun_smoke"),
            .muzzleFlash = load("muzzle_flash"),
            .casings = load("shell_casings"),
            .impactDust = load("impact_dust"),
            .impactChips = load("impact_chips"),
            .impactSplinters = load("impact_splinters"),
            .impactGlass = load("impact_glass"),
            .barrelSmoke = load("barrel_smoke"),
            .hole = Decal(scene, directory, "bullet_hole.png", resources::TextureUse::Color),
            .holeNormal = Decal(scene, directory, "bullet_hole_normal.png", resources::TextureUse::Normal),
            .holeMetal = Decal(scene, directory, "bullet_hole_metal.png", resources::TextureUse::Color),
            .bloodSplatter = Decal(scene, directory, "blood_splatter.png", resources::TextureUse::Color)};
}

std::optional<PickupKind> PickupOf(std::string_view kind) {
    if (kind == "health") {
        return PickupKind::Health;
    }
    if (kind == "armor") {
        return PickupKind::Armor;
    }
    if (kind == "handgun_ammo") {
        return PickupKind::HandgunAmmo;
    }
    if (kind == "shotgun_ammo") {
        return PickupKind::ShotgunAmmo;
    }
    if (kind == "flashlight") {
        return PickupKind::Flashlight;
    }
    if (kind == "card") {
        return PickupKind::RedCard;
    }
    if (kind == "shotgun") {
        return PickupKind::Shotgun;
    }
    return std::nullopt;
}

void Say(Player& player, std::string message) {
    player.message = std::move(message);
    player.messageTime = kMessageTime;
}

SoundBank LoadSounds(scene::Scene& scene) {
    const auto start = std::chrono::steady_clock::now();
    SoundBank bank;
    bank.steps = {LoadSound(scene, "footstep1.wav"), LoadSound(scene, "footstep2.wav"),
                  LoadSound(scene, "footstep3.wav"), LoadSound(scene, "footstep4.wav")};
    bank.pickups[static_cast<std::size_t>(PickupKind::Health)] = LoadSound(scene, "health_bonus.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::Armor)] = LoadSound(scene, "armor_bonus.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::HandgunAmmo)] = LoadSound(scene, "handgun_ammo.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::ShotgunAmmo)] = LoadSound(scene, "shotgun_ammo.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::Flashlight)] = LoadSound(scene, "flashlight_bonus.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::RedCard)] = LoadSound(scene, "misc_bonus.wav");
    bank.pickups[static_cast<std::size_t>(PickupKind::Shotgun)] = LoadSound(scene, "weapon_bonus.wav");
    for (const char* surface : {"concrete", "metal", "wood", "tile", "water"}) {
        for (int n = 1; n <= 4; ++n) {
            bank.surfaceSteps[surface].push_back(LoadSound(scene, std::format("Surfaces/step_{}_{}.wav", surface, n)));
        }
    }
    for (const char* surface : {"concrete", "metal", "wood", "tile", "glass", "flesh"}) {
        for (int n = 1; n <= 3; ++n) {
            bank.surfaceImpacts[surface].push_back(
                LoadSound(scene, std::format("Surfaces/impact_{}_{}.wav", surface, n)));
        }
    }
    bank.jump = LoadSound(scene, "jump.wav");
    bank.land = LoadSound(scene, "footstep_fall.wav");
    bank.ladder = LoadSound(scene, "ladder.wav");
    bank.flashlight = LoadSound(scene, "flashlight.wav");
    bank.doorStart = LoadSound(scene, "door_start.wav");
    bank.doorStop = LoadSound(scene, "door_stop.wav");
    bank.alarm = LoadSound(scene, "alarm.wav");
    bank.fan = LoadSound(scene, "fan.wav");
    bank.music = LoadSound(scene, "enter_the_maze.ogg", {.stream = true});
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    DAI_LOG_INFO("sandbox", "{} sounds loaded in {:.0f} ms", scene.Sounds().ClipCount(), elapsed.count());
    return bank;
}

/// Starts what plays for the whole level. Waiting for the scene to run keeps a restart, which builds
/// the new scene while the old one plays, from doubling every sound.
void StartLoopedSounds(scene::Scene& scene, ecs::Entity playerEntity) {
    ecs::World& world = scene.World();
    audio::SoundWorld& sounds = scene.Sounds();
    if (const SoundBank* bank = world.TryGet<SoundBank>(playerEntity)) {
        sounds.Play(bank->music, {.volume = kMusicVolume, .loop = true, .bus = audio::Bus::Music});
    }
    std::vector<std::pair<ecs::Entity, audio::VoiceHandle>> started;
    world.Each<LoopedSound, ecs::LocalTransform>(
        [&](ecs::Entity entity, const LoopedSound& sound, const ecs::LocalTransform& transform) {
            started.emplace_back(entity, sounds.Play(sound.clip, {.position = transform.translation,
                                                                  .volume = sound.volume,
                                                                  .loop = true,
                                                                  // Hums and drones: ambience (ADR-0067).
                                                                  .bus = audio::Bus::Ambience,
                                                                  .minDistance = sound.minDistance,
                                                                  .maxDistance = sound.maxDistance}));
        });
    for (const auto& [entity, voice] : started) {
        world.Add<scene::SoundEmitter>(entity, voice);
    }
}

void Collect(Player& player, PickupKind kind) {
    switch (kind) {
    case PickupKind::Health:
        player.health = std::min(player.health + kHealthBonus, kMaxHealth);
        Say(player, "Health");
        break;
    case PickupKind::Armor:
        player.armor += kArmorBonus;
        Say(player, "Armor");
        break;
    case PickupKind::HandgunAmmo:
        AddAmmo(player, WeaponKind::Handgun, kHandgunAmmoBonus);
        Say(player, "Handgun ammo");
        break;
    case PickupKind::ShotgunAmmo:
        AddAmmo(player, WeaponKind::Shotgun, kShotgunAmmoBonus);
        Say(player, "Shotgun shells");
        break;
    case PickupKind::Shotgun:
        GiveShotgun(player);
        Say(player, "Shotgun: press 2");
        break;
    case PickupKind::Flashlight:
        // A second flashlight is a fresh battery.
        if (player.hasFlashlight) {
            player.torch.battery = 1.0f;
            Say(player, "Fresh batteries");
        } else {
            Say(player, "Flashlight: press F");
        }
        player.hasFlashlight = true;
        player.flashlightOn = true;
        break;
    case PickupKind::RedCard:
        player.hasRedCard = true;
        Say(player, "Red access card");
        break;
    }
}

/// What the player does every frame: looking around, and what must not miss a press.
void LookAround(ecs::Entity entity, scene::Scene& scene, const input::InputState& input,
                const input::ActionMap& actions, float mouseSensitivity, float delta) {
    ecs::World& world = scene.World();
    Player& player = world.Get<Player>(entity);
    ecs::LocalTransform& transform = world.Get<ecs::LocalTransform>(entity);

    player.yaw -= input.MouseDelta().x * mouseSensitivity;
    player.pitch -= input.MouseDelta().y * mouseSensitivity;
    for (const platform::GamepadId gamepad : input.Gamepads()) {
        using platform::GamepadAxis;
        const auto stick = [&](GamepadAxis axis) {
            const float value = input.Axis(gamepad, axis);
            return std::abs(value) < input::ActionMap::kDefaultDeadZone ? 0.0f : value;
        };
        player.yaw -= stick(GamepadAxis::RightX) * kStickLookSpeed * delta;
        player.pitch += stick(GamepadAxis::RightY) * kStickLookSpeed * delta;
    }
    player.pitch = std::clamp(player.pitch, -kMaxPitch, kMaxPitch);
    transform.rotation = math::FromYawPitchRoll(player.yaw, player.pitch, 0.0f);

    const float targetEye = player.dead ? kDeadEyeHeight : (player.crouching ? kCrouchEyeHeight : kEyeHeight);
    const float eyeStep = kEyeSpeed * delta;
    player.eyeHeight = std::clamp(targetEye, player.eyeHeight - eyeStep, player.eyeHeight + eyeStep);
    transform.translation =
        glm::mix(player.previousFeet, player.feet, scene.FixedAlpha()) + math::kUp * player.eyeHeight;

    player.jumpPressed = player.jumpPressed || actions.WasPressed(input, Jump);
    player.interactPressed = player.interactPressed || actions.WasPressed(input, Interact);
    if (player.hasFlashlight && actions.WasPressed(input, ToggleFlashlight)) {
        player.flashlightOn = !player.flashlightOn;
        if (const SoundBank* bank = scene.World().TryGet<SoundBank>(entity)) {
            scene.Sounds().Play(bank->flashlight);
        }
    }
    player.messageTime = std::max(player.messageTime - delta, 0.0f);
}

/// The top of the ladder the feet are at, if any.
std::optional<float> LadderTop(ecs::World& world, const glm::vec3& feet) {
    std::optional<float> top;
    world.Each<Ladder>([&](ecs::Entity /*entity*/, const Ladder& ladder) {
        if (feet.x > ladder.min.x - kLadderReach && feet.x < ladder.max.x + kLadderReach &&
            feet.z > ladder.min.z - kLadderReach && feet.z < ladder.max.z + kLadderReach &&
            feet.y > ladder.min.y - 0.1f && feet.y < ladder.max.y + kLadderTopMargin + 0.1f) {
            top = std::max(top.value_or(ladder.max.y), ladder.max.y);
        }
    });
    return top;
}

/// What the player stands on, for the sound of a step (ADR-0067): water where it pools, else the surface of
/// the body under the feet; what has no surface or no sound of its own sounds like concrete.
std::string_view GroundSurface(scene::Scene& scene, ecs::Entity entity, const Player& player, const SoundBank& bank) {
    ecs::World& world = scene.World();
    bool inWater = false;
    world.Each<scene::WetVolume, ecs::WorldTransform>([&](ecs::Entity, const scene::WetVolume& wet,
                                                          const ecs::WorldTransform& transform) {
        const glm::vec3 local = glm::vec3(glm::inverse(transform.matrix) * glm::vec4(player.feet, 1.0f));
        const glm::vec3 size{glm::length(glm::vec3(transform.matrix[0])), glm::length(glm::vec3(transform.matrix[1])),
                             glm::length(glm::vec3(transform.matrix[2]))};
        inWater = inWater ||
                  (wet.enabled && wet.puddles > 0.2f && glm::all(glm::lessThan(glm::abs(local), glm::vec3(0.5f))) &&
                   (local.y + 0.5f) * size.y < wet.puddleHeight);
    });
    if (inWater) {
        return "water";
    }
    const physics::PhysicsWorld& physics = scene.Physics();
    const auto hit =
        physics.CastRay(player.feet + math::kUp * 0.3f, -math::kUp, 1.0f, physics.CharacterBody(player.character));
    const BodySurfaces* surfaces = world.TryGet<BodySurfaces>(entity);
    const std::string_view surface =
        hit.has_value() && surfaces != nullptr ? surfaces->Of(hit->body) : std::string_view{};
    return bank.surfaceSteps.contains(surface) ? surface : std::string_view("concrete");
}

} // namespace

void PlayImpact(scene::Scene& scene, ecs::Entity player, std::string_view surface, const glm::vec3& position) {
    SoundBank* bank = scene.World().TryGet<SoundBank>(player);
    if (bank == nullptr) {
        return;
    }
    auto impacts = bank->surfaceImpacts.find(surface);
    if (impacts == bank->surfaceImpacts.end()) {
        impacts = bank->surfaceImpacts.find("concrete");
    }
    if (impacts != bank->surfaceImpacts.end() && !impacts->second.empty()) {
        scene.Sounds().Play(impacts->second[bank->random() % impacts->second.size()],
                            {.position = position, .volume = 0.8f, .minDistance = 1.0f, .maxDistance = 40.0f});
    }
}

namespace {

/// Footsteps go by the distance walked, so that they follow the speed of the player.
void PlayMovementSounds(scene::Scene& scene, ecs::Entity entity, bool climbing, bool grounded) {
    ecs::World& world = scene.World();
    SoundBank* bank = world.TryGet<SoundBank>(entity);
    if (bank == nullptr) {
        return;
    }
    const Player& player = world.Get<Player>(entity);
    const glm::vec3 moved = player.feet - player.previousFeet;
    if (climbing) {
        bank->ladderDistance += std::abs(moved.y);
        if (bank->ladderDistance >= kLadderDistance) {
            bank->ladderDistance = 0.0f;
            scene.Sounds().Play(bank->ladder);
        }
    } else if (grounded) {
        bank->stepDistance += glm::length(glm::vec2(moved.x, moved.z));
        if (bank->stepDistance >= kStepDistance) {
            bank->stepDistance = 0.0f;
            // Never the same one twice in a row.
            const std::size_t offset = 1 + bank->random() % (bank->steps.size() - 1);
            bank->lastStep = (bank->lastStep + offset) % bank->steps.size();
            // The step of the surface underfoot, over the old step's thud.
            const auto steps = bank->surfaceSteps.find(GroundSurface(scene, entity, player, *bank));
            if (steps != bank->surfaceSteps.end() && !steps->second.empty()) {
                scene.Sounds().Play(steps->second[bank->lastStep % steps->second.size()], {.volume = 0.7f});
            }
            scene.Sounds().Play(bank->steps[bank->lastStep], {.volume = 0.5f});
            // In a puddle, the step rings the water (ADR-0061); elsewhere the ring shows on nothing.
            scene.AddRipple(player.feet, 1.5f);
        }
    } else {
        bank->stepDistance = 0.0f;
    }
}

/// What the player does on every fixed step: walking, jumping, crouching and climbing.
void MovePlayer(ecs::Entity entity, scene::Scene& scene, const input::InputState& input,
                const input::ActionMap& actions, float step) {
    ecs::World& world = scene.World();
    physics::PhysicsWorld& physics = scene.Physics();
    Player& player = world.Get<Player>(entity);

    const bool wantsCrouch = actions.IsDown(input, Crouch) && !player.dead;
    if (wantsCrouch != player.crouching &&
        physics.SetCharacterHeight(player.character, wantsCrouch ? kCrouchHeight : kStandHeight)) {
        player.crouching = wantsCrouch;
    }

    // Only the direction the player faces counts: looking up does not slow them down.
    const glm::quat heading = math::FromYawPitchRoll(player.yaw, 0.0f, 0.0f);
    const float forward = actions.Value(input, MoveForward);
    glm::vec3 wish = heading * glm::vec3(actions.Value(input, MoveRight), 0.0f, -forward);
    if (player.dead) {
        wish = glm::vec3(0.0f);
        player.jumpPressed = false;
    }
    if (const float length = glm::length(wish); length > 1.0f) {
        wish /= length;
    }
    float speed = kWalkSpeed;
    if (player.crouching) {
        speed *= kCrouchFactor;
    } else if (actions.IsDown(input, Run)) {
        speed *= kRunFactor;
    }
    glm::vec3 velocity = wish * speed;

    const bool jump = player.jumpPressed;
    player.jumpPressed = false;
    // Moving up, the ground just left does not count as ground yet.
    const bool onGround = physics.IsOnGround(player.character) && player.verticalSpeed <= 0.0f;
    const SoundBank* bank = world.TryGet<SoundBank>(entity);
    const std::optional<float> top = LadderTop(world, player.feet);
    const bool climbing = top.has_value() && !jump;
    if (climbing) {
        player.verticalSpeed = forward * kClimbSpeed * (player.pitch < kLookDownOnLadder ? -1.0f : 1.0f);
        // The player waits just above the top, from where they can step off to any side.
        if (player.feet.y >= *top + kLadderTopMargin) {
            player.verticalSpeed = std::min(player.verticalSpeed, 0.0f);
        }
    } else if (onGround) {
        if (bank != nullptr && player.verticalSpeed <= -kLandingSpeed) {
            scene.Sounds().Play(bank->land);
        }
        const bool jumps = jump && !player.crouching;
        if (bank != nullptr && jumps) {
            scene.Sounds().Play(bank->jump);
        }
        player.verticalSpeed = jumps ? kJumpSpeed : 0.0f;
    } else {
        // A ceiling takes the speed away.
        player.verticalSpeed =
            std::min(player.verticalSpeed, physics.CharacterVelocity(player.character).y) - kGravity * step;
    }
    velocity.y = player.verticalSpeed;

    physics.MoveCharacter(player.character, velocity, step);
    player.previousFeet = player.feet;
    player.feet = physics.CharacterPosition(player.character);
    PlayMovementSounds(scene, entity, climbing, onGround);
}

void UpdatePickups(scene::Scene& scene, ecs::Entity playerEntity, float delta) {
    ecs::World& world = scene.World();
    const SoundBank* bank = world.TryGet<SoundBank>(playerEntity);
    Player& player = world.Get<Player>(playerEntity);
    const glm::vec3 eye = world.Get<ecs::LocalTransform>(playerEntity).translation;
    const glm::quat spin = glm::angleAxis(kPickupSpin * delta, math::kUp);

    std::vector<ecs::Entity> taken;
    world.Each<Pickup, ecs::LocalTransform>(
        [&](ecs::Entity entity, const Pickup& pickup, ecs::LocalTransform& transform) {
            transform.rotation = spin * transform.rotation;
            if (glm::distance(transform.translation, eye) < kPickupRadius + kEyeHeight * 0.5f) {
                Collect(player, pickup.kind);
                if (bank != nullptr) {
                    scene.Sounds().Play(bank->pickups[static_cast<std::size_t>(pickup.kind)]);
                }
                taken.push_back(entity);
            }
        });
    for (const ecs::Entity entity : taken) {
        world.Destroy(entity);
    }
}

void UpdateDoors(scene::Scene& scene, ecs::Entity playerEntity, float delta) {
    ecs::World& world = scene.World();
    const SoundBank* bank = world.TryGet<SoundBank>(playerEntity);
    const auto playAt = [&](audio::ClipHandle(SoundBank::* clip), const glm::vec3& position) {
        if (bank != nullptr) {
            scene.Sounds().Play(bank->*clip, {.position = position});
        }
    };
    Player& player = world.Get<Player>(playerEntity);
    const glm::vec3 eye = world.Get<ecs::LocalTransform>(playerEntity).translation;
    const bool interact = player.interactPressed;
    player.interactPressed = false;

    world.Each<Door, ecs::LocalTransform>([&](ecs::Entity /*entity*/, Door& door, ecs::LocalTransform& transform) {
        const float distance = glm::distance(door.closedPosition, eye);
        const bool byHand = door.radius < 0.0f;
        if (!door.open && (byHand ? interact && distance < kDoorRadius : distance < door.radius)) {
            if (door.locked && !player.hasRedCard) {
                Say(player, "Locked: it needs a red card");
            } else {
                door.locked = false;
                door.open = true;
                door.timeLeft = kDoorOpenTime;
                playAt(&SoundBank::doorStart, door.closedPosition);
            }
        }
        if (door.open && door.closable) {
            door.timeLeft -= delta;
            // Never down on the player.
            if (door.timeLeft <= 0.0f && distance >= kDoorRadius) {
                door.open = false;
                playAt(&SoundBank::doorStart, door.closedPosition);
            }
        }
        const float step = kDoorSpeed * delta;
        const float rise = std::clamp(door.rise + (door.open ? step : -step), 0.0f, kDoorRise);
        if (rise != door.rise && (rise == 0.0f || rise == kDoorRise)) {
            playAt(&SoundBank::doorStop, door.closedPosition);
        }
        door.rise = rise;
        transform.translation = door.closedPosition + math::kUp * door.rise;
    });
}

/// The level's exposure and haze (ADR-0054), which follow the darkness setting. The sky stays as the level set it.
struct LevelLook {
    scene::LevelEnvironment environment;
};

void ApplyAtmosphere(scene::Scene& scene, const Settings& settings) {
    scene::LevelEnvironment environment;
    scene.World().Each<LevelLook>(
        [&environment](ecs::Entity /*entity*/, const LevelLook& look) { environment = look.environment; });
    const float darkness = std::clamp(settings.darkness, 0.0f, 1.0f);
    // Brightness is seen in stops: halfway is half the stops.
    const float ev100 = environment.ev100 - ((1.0f - darkness) * kBrightStops);
    renderer::Atmosphere atmosphere = scene.Atmosphere();
    atmosphere.fog = {.density = environment.fogDensity * darkness,
                      .albedo = environment.fogAlbedo,
                      .anisotropy = environment.fogAnisotropy};
    atmosphere.ev100 = ev100;
    atmosphere.autoExposure = environment.autoExposure;
    atmosphere.ev100Min = ev100 - environment.exposureMin;
    atmosphere.ev100Max = ev100 + environment.exposureMax;
    scene.SetAtmosphere(atmosphere);
}

} // namespace

input::ActionMap MakeActions() {
    using platform::GamepadAxis;
    using platform::GamepadButton;
    using platform::Key;

    input::ActionMap actions;
    actions.BindAxis(MoveForward, Key::W, Key::S);
    actions.BindAxis(MoveForward, GamepadAxis::LeftY);
    actions.BindAxis(MoveRight, Key::D, Key::A);
    actions.BindAxis(MoveRight, GamepadAxis::LeftX);
    actions.Bind(Jump, Key::Space);
    actions.Bind(Jump, GamepadButton::South);
    actions.Bind(Crouch, Key::C);
    actions.Bind(Crouch, Key::LeftCtrl);
    actions.Bind(Crouch, GamepadButton::East);
    actions.Bind(Run, Key::LeftShift);
    actions.Bind(Run, GamepadButton::LeftShoulder);
    actions.Bind(Interact, Key::E);
    actions.Bind(Interact, GamepadButton::West);
    actions.Bind(ToggleFlashlight, Key::F);
    actions.Bind(ToggleFlashlight, GamepadButton::North);
    actions.Bind(Restart, Key::F5);
    actions.Bind(ToggleMouse, Key::Tab);
    actions.Bind(Menu, Key::Escape);
    actions.Bind(Menu, GamepadButton::Start);
    actions.Bind(Menu, GamepadButton::Back);
    actions.Bind(Fire, platform::MouseButton::Left);
    actions.BindAxis(FireTrigger, GamepadAxis::RightTrigger);
    actions.Bind(Reload, Key::R);
    actions.Bind(Reload, GamepadButton::RightShoulder);
    actions.Bind(NextWeapon, Key::Q);
    actions.Bind(NextWeapon, GamepadButton::DpadUp);
    actions.Bind(SelectHandgun, Key::Digit1);
    actions.Bind(SelectShotgun, Key::Digit2);
    return actions;
}

audio::ClipHandle LoadSound(scene::Scene& scene, std::string_view name, const audio::ClipOptions& options) {
    core::Result<audio::ClipHandle> clip =
        scene.Sounds().Load(std::string("Factory/Sounds/") + std::string(name), options);
    if (!clip) {
        DAI_LOG_WARNING("sandbox", "{}", core::Format(clip.error()));
        return {};
    }
    return *clip;
}

namespace {

// A door's geometry in the acoustic scene (ADR-0065), moved where the door is.
struct DoorAcoustics {
    std::size_t mesh = 0;
    glm::mat4 placed{1.0f};
};

} // namespace

namespace {

/// Plays the first clip of a model forever, from a random time, so that no two turn in step.
void PlayForever(ecs::World& world, ecs::Entity model, std::minstd_rand& random) {
    if (animation::Animator* animator = world.TryGet<animation::Animator>(model)) {
        const animation::ClipSet& clips = animator->Clips();
        if (!clips.clips.empty()) {
            std::uniform_real_distribution<float> start(0.0f, clips.clips.front().duration);
            animator->Play(clips.clips.front().name, {.fade = 0.0f, .startTime = start(random)});
        }
    }
}

} // namespace

void Teleport(scene::Scene& scene, ecs::Entity entity, const glm::vec3& eye) {
    Player& player = scene.World().Get<Player>(entity);
    player.feet = eye - math::kUp * player.eyeHeight;
    player.previousFeet = player.feet;
    player.verticalSpeed = 0.0f;
    scene.Physics().SetCharacterPosition(player.character, player.feet);
    scene.CutView();
}

core::Result<ecs::Entity> BuildLevel(scene::Scene& scene, const scene::Level& level, std::string_view levelPath) {
    ecs::World& world = scene.World();
    scene::LevelInstance instance(scene, levelPath);
    instance.Build(level);
    ecs::PropagateTransforms(world);
    physics::PhysicsWorld& physics = scene.Physics();

    // What the game needs from the models' geometry: the level to walk on, what each of its parts is made of,
    // and the size of the doors, from the nodes tagged "level" and "door".
    BodySurfaces surfaces;
    struct DoorShape {
        const scene::Model* model = nullptr;
        const scene::Model::Node* node = nullptr;
    };
    std::map<std::uint64_t, DoorShape> doorShapes;
    std::vector<std::pair<ecs::Entity, const scene::ModelInstance*>> instances;
    world.Each<scene::ModelInstance>([&instances](ecs::Entity entity, const scene::ModelInstance& placed) {
        instances.emplace_back(entity, &placed);
    });
    for (const auto& [owner, placed] : instances) {
        for (const scene::Model::Node& node : placed->model->nodes) {
            const std::string kind = scene::ParseTags(node.name).kind;
            if (kind != "level" && kind != "door") {
                continue;
            }
            const ecs::Entity nodeEntity = scene::FindNodeEntity(world, placed->root, node.name);
            if (nodeEntity.IsNull()) {
                continue;
            }
            if (kind == "door") {
                doorShapes[nodeEntity.Index()] = {placed->model, &node};
                continue;
            }
            // In world space: the model may be placed anywhere.
            const glm::mat4& placement = world.Get<ecs::WorldTransform>(nodeEntity).matrix;
            for (const std::uint32_t index : node.parts) {
                const scene::Model::Part& part = placed->model->parts[index];
                std::vector<glm::vec3> positions;
                positions.reserve(part.positions.size());
                for (const glm::vec3& position : part.positions) {
                    positions.push_back(math::TransformPoint(placement, position));
                }
                DAI_TRY_ASSIGN(physics::BodyId body, physics.AddStaticMesh(positions, part.indices, math::Transform{}));
                // Sound meets the same walls and floors (ADR-0065), made of the part's surface.
                scene.Sounds().AddAcousticMesh(positions, part.indices, audio::AcousticMaterialOf(part.surface));
                if (!part.surface.empty()) {
                    surfaces.surfaces.emplace(body.value, part.surface);
                }
            }
        }
    }

    // Sound finds its way around the walls (ADR-0066): the paths bake on a worker thread while the game starts.
    scene.Sounds().StartPathing(3.0f);

    ecs::Entity playerEntity = world.Create();
    glm::vec3 eye{0.0f, kEyeHeight, 3.0f};
    Player& newPlayer = world.Add<Player>(playerEntity);
    world.Add<scene::Camera>(playerEntity);
    world.Add<LevelLook>(playerEntity, level.environment);
    // The lights that the player carries follow the camera.
    const glm::vec3 flashlightAim = glm::normalize(math::kForward * kFlashlightAimDistance - kFlashlightHand);
    newPlayer.flashlight = AddLight(world, playerEntity, kFlashlightHand, glm::quatLookAtRH(flashlightAim, math::kUp),
                                    {.type = renderer::LightType::Spot,
                                     .color = kFlashlightColor,
                                     .intensity = kFlashlightCandela,
                                     .range = kFlashlightRange,
                                     .innerAngle = kFlashlightInnerAngle,
                                     .outerAngle = kFlashlightOuterAngle,
                                     .enabled = false});
    // The beam of a real torch: a hot spot, its ring and spill, through a lens (ADR-0058).
    {
        const std::string levelDirectory = std::filesystem::path(levelPath).parent_path().generic_string();
        const std::string lights = levelDirectory.empty() ? "Lights/" : levelDirectory + "/Lights/";
        scene::LightTextures textures;
        if (core::Result<rhi::TextureHandle> profile = scene.Assets().LoadLightProfile(lights + "flashlight.ies")) {
            textures.profile = *profile;
        }
        if (core::Result<rhi::TextureHandle> lens =
                scene.Assets().LoadTexture(lights + "flashlight_cookie.png", resources::TextureUse::Data)) {
            textures.cookie = *lens;
        }
        world.Add<scene::LightTextures>(newPlayer.flashlight, textures);
    }
    world.Get<Player>(playerEntity).muzzleFlash =
        AddLight(world, playerEntity, math::kForward * 0.7f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                 {.type = renderer::LightType::Point,
                  .color = kMuzzleFlashColor,
                  .intensity = kMuzzleFlashCandela,
                  .range = kMuzzleFlashRange,
                  .enabled = false});
    physics::CharacterConfig body;
    body.height = kStandHeight;
    world.Get<Player>(playerEntity).character = physics.AddCharacter(body, eye - math::kUp * kEyeHeight);
    const SoundBank& bank = world.Add<SoundBank>(playerEntity, LoadSounds(scene));
    // The weapons and the effects the game spawns are found next to the level.
    const std::string directory = std::filesystem::path(levelPath).parent_path().generic_string();
    const physics::BodyId playerBody = physics.CharacterBody(world.Get<Player>(playerEntity).character);

    std::vector<int> lockedDoors;
    std::vector<std::pair<ecs::Entity, scene::Tags>> tagged;
    // A model placed by a game object (a zombie, a fan, a pickup) is that object: the tags of its own nodes are
    // not the level's.
    std::vector<ecs::Entity> objectModels;
    world.Each<scene::Tags, scene::ModelInstance>(
        [&objectModels](ecs::Entity /*entity*/, const scene::Tags& /*tags*/, const scene::ModelInstance& placed) {
            objectModels.push_back(placed.root);
        });
    world.Each<scene::Tags>([&](ecs::Entity entity, const scene::Tags& tags) {
        const bool inObject = std::ranges::any_of(
            objectModels, [&](ecs::Entity root) { return entity == root || world.IsDescendantOf(entity, root); });
        if (!inObject) {
            tagged.emplace_back(entity, tags);
        }
    });
    for (const auto& [entity, tags] : tagged) {
        if (tags.kind == "card_reader") {
            lockedDoors.push_back(Number(tags, "id", 0));
        }
    }
    world.Add<ParticleEffects>(playerEntity, LoadParticleEffects(scene, directory));
    // After the level's tags are listed: the weapons have tagged nodes too, which are not the level's.
    SetUpCombat(scene, playerEntity, directory);
    std::minstd_rand random{std::random_device{}()};
    for (const auto& [entity, tags] : tagged) {
        const glm::vec3 position = world.Get<ecs::WorldTransform>(entity).matrix[3];
        const scene::ModelInstance* placed = world.TryGet<scene::ModelInstance>(entity);
        if (tags.kind == "zombie" && placed != nullptr) {
            AddZombie(scene, entity, placed->root, tags.Value("patrol"));
            ++world.Get<Player>(playerEntity).zombieCount;
        } else if (tags.kind == "fan" && placed != nullptr) {
            PlayForever(world, placed->root, random);
            world.Add<LoopedSound>(entity, bank.fan, 0.7f, 1.0f, 20.0f);
        } else if (tags.kind == "player") {
            eye = position;
            if (const std::optional<physics::RayHit> floor = physics.CastRay(position, -math::kUp, 20.0f, playerBody)) {
                eye = floor->position + math::kUp * kEyeHeight;
            }
        } else if (tags.kind == "alarm") {
            world.Add<LoopedSound>(entity, bank.alarm, 0.5f, 1.4f, 35.0f);
            world.Add<Alarm>(entity, Number(tags, "rotation_speed", kAlarmSpeed));
        } else if (tags.kind == "ladder") {
            world.Add<Ladder>(entity, Vector(tags, "min"), Vector(tags, "max"));
        } else if (const std::optional<PickupKind> pickup = PickupOf(tags.kind)) {
            world.Add<Pickup>(entity, *pickup);
        } else if (tags.kind == "door") {
            Door door;
            door.id = Number(tags, "id", 0);
            door.closedPosition = world.Get<ecs::LocalTransform>(entity).translation;
            door.radius = Number(tags, "radius", 1.0f) < 0.0f ? -1.0f : kDoorRadius;
            door.closable = tags.Value("closable") != "false";
            door.locked = std::ranges::find(lockedDoors, door.id) != lockedDoors.end();
            world.Add<Door>(entity, door);

            if (const auto found = doorShapes.find(entity.Index()); found != doorShapes.end()) {
                const scene::Model& model = *found->second.model;
                glm::vec3 low(std::numeric_limits<float>::max());
                glm::vec3 high(std::numeric_limits<float>::lowest());
                for (const std::uint32_t index : found->second.node->parts) {
                    for (const glm::vec3& vertex : model.parts[index].positions) {
                        low = glm::min(low, vertex);
                        high = glm::max(high, vertex);
                    }
                }
                if (low.x <= high.x) {
                    math::Transform transform;
                    transform.translation = door.closedPosition;
                    const glm::vec3 halfExtents = glm::max((high - low) * 0.5f, glm::vec3(0.05f));
                    DAI_TRY_ASSIGN(physics::BodyId box,
                                   physics.AddKinematicBox(halfExtents, (low + high) * 0.5f, transform));
                    world.Add<scene::KinematicBody>(entity, box);
                    // A door is made of what its first part with a surface is made of.
                    std::string_view surface;
                    for (const std::uint32_t index : found->second.node->parts) {
                        if (!model.parts[index].surface.empty()) {
                            surfaces.surfaces.emplace(box.value, model.parts[index].surface);
                            surface = model.parts[index].surface;
                            break;
                        }
                    }
                    // In the acoustic scene, the door moves with its entity: closed, it muffles what is behind.
                    std::vector<glm::vec3> positions;
                    std::vector<std::uint32_t> indices;
                    for (const std::uint32_t index : found->second.node->parts) {
                        const auto first = static_cast<std::uint32_t>(positions.size());
                        positions.insert(positions.end(), model.parts[index].positions.begin(),
                                         model.parts[index].positions.end());
                        for (const std::uint32_t vertex : model.parts[index].indices) {
                            indices.push_back(first + vertex);
                        }
                    }
                    const glm::mat4& doorPlacement = world.Get<ecs::WorldTransform>(entity).matrix;
                    world.Add<DoorAcoustics>(
                        entity, DoorAcoustics{scene.Sounds().AddMovableAcousticMesh(
                                                  positions, indices,
                                                  audio::AcousticMaterialOf(surface.empty() ? "metal" : surface),
                                                  doorPlacement),
                                              doorPlacement});
                }
            }
        }
    }
    world.Add<BodySurfaces>(playerEntity, std::move(surfaces));
    ecs::LocalTransform start;
    start.translation = eye;
    world.Add<ecs::LocalTransform>(playerEntity, start);
    Teleport(scene, playerEntity, eye);
    scene.SetActiveCamera(playerEntity);
    return playerEntity;
}

/// A wisp of the showcase, which circles its center.
struct Circling {
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    /// Radians per second.
    float speed = 1.0f;
    float angle = 0.0f;
};

void AddEffectShowcase(scene::Scene& scene, std::string_view levelPath, const glm::vec3& eye, float yaw) {
    ecs::World& world = scene.World();
    const std::string directory = std::filesystem::path(levelPath).parent_path().generic_string();
    // Yaw 0 looks along -Z, and turns left as it grows.
    const glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
    const glm::vec3 floor = eye - math::kUp * kEyeHeight;
    const auto place = [&](const glm::vec3& position, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
        const ecs::Entity entity = world.Create();
        ecs::LocalTransform transform;
        transform.translation = position;
        transform.rotation = rotation;
        world.Add<ecs::LocalTransform>(entity, transform);
        return entity;
    };
    const auto ribbon = [&](std::string_view name, ecs::Entity entity, ecs::Entity target = {}) {
        core::Result<std::shared_ptr<const scene::RibbonEffect>> effect =
            scene.Assets().LoadRibbonEffect(RibbonPath(directory, name));
        if (!effect) {
            DAI_LOG_WARNING("sandbox", "Showcase: {}", core::Format(effect.error()));
            return;
        }
        world.Add<scene::RibbonEmitter>(entity, scene::RibbonEmitter{.effect = *effect, .target = target});
    };
    const auto particles = [&](std::string_view name, ecs::Entity entity) {
        core::Result<scene::ParticleEffectAsset> effect =
            scene.Assets().LoadParticleEffect(EffectPath(directory, name));
        if (!effect) {
            DAI_LOG_WARNING("sandbox", "Showcase: {}", core::Format(effect.error()));
            return;
        }
        world.Add<scene::ParticleEmitter>(entity, effect->effect, effect->emission);
    };
    // An arc between two points at hand's height, on the left.
    const glm::vec3 arcStart = floor + forward * 3.0f - right * 1.2f + math::kUp * 1.2f;
    ribbon("arc", place(arcStart), place(arcStart + right * 0.8f + math::kUp * 0.1f));
    // A laser across the view, low.
    ribbon("laser", place(floor + forward * 2.5f - right * 3.0f + math::kUp * 0.6f,
                          glm::quat(math::kUp, glm::normalize(right + forward * 0.15f))));
    // Lightning, from high up to the floor, ahead on the right.
    const glm::vec3 boltEnd = floor + forward * 6.0f + right * 2.0f;
    ribbon("lightning", place(boltEnd + math::kUp * 4.0f), place(boltEnd));
    // Glass that breaks again and again, papers in the air, ahead.
    particles("glass_shards", place(floor + forward * 4.0f + right * 1.0f + math::kUp * 1.5f,
                                    glm::quat(math::kUp, glm::normalize(-forward + math::kUp))));
    particles("papers", place(floor + forward * 4.0f + math::kUp * 2.5f));
    // A wisp that circles in front.
    const ecs::Entity wisp = place(floor + forward * 4.0f + math::kUp * 1.6f);
    ribbon("wisp", wisp);
    world.Add<Circling>(wisp,
                        Circling{.center = floor + forward * 4.0f + math::kUp * 1.6f, .radius = 1.0f, .speed = 1.6f});
    scene.FrameSystems().Add("Showcase", [](ecs::World& systemWorld, core::Seconds delta) {
        systemWorld.Each<Circling, ecs::LocalTransform>(
            [&](ecs::Entity /*entity*/, Circling& circling, ecs::LocalTransform& transform) {
                circling.angle += circling.speed * static_cast<float>(delta.count());
                transform.translation = circling.center + glm::vec3(std::cos(circling.angle) * circling.radius,
                                                                    0.3f * std::sin(circling.angle * 2.3f),
                                                                    std::sin(circling.angle) * circling.radius);
            });
    });
}

void AddSystems(scene::Scene& scene, const input::InputState& input, const input::ActionMap& actions,
                const Settings& settings) {
    const auto player = [&scene] {
        ecs::Entity found;
        scene.World().Each<Player>([&found](ecs::Entity entity, const Player& /*player*/) { found = entity; });
        return found;
    };
    // Also before the first frame, for a scene that starts paused behind the main menu.
    ApplyAtmosphere(scene, settings);
    ecs::Schedule& fixed = scene.FixedSystems();
    fixed.Add("Movement", [&, player](ecs::World& world, core::Seconds step) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            MovePlayer(entity, scene, input, actions, static_cast<float>(step.count()));
        }
    });
    fixed.Add("Doors", [&scene, player](ecs::World& world, core::Seconds step) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdateDoors(scene, entity, static_cast<float>(step.count()));
        }
        // Doors that moved move in the acoustic scene too.
        world.Each<DoorAcoustics, ecs::WorldTransform>(
            [&scene](ecs::Entity /*door*/, DoorAcoustics& acoustics, const ecs::WorldTransform& transform) {
                if (transform.matrix != acoustics.placed) {
                    acoustics.placed = transform.matrix;
                    scene.Sounds().MoveAcousticMesh(acoustics.mesh, transform.matrix);
                }
            });
    });

    ecs::Schedule& systems = scene.FrameSystems();
    // The mix follows the player (ADR-0067): a wound muffles the world for a moment, death for good.
    systems.Add("Mix", [&scene, player](ecs::World& world, core::Seconds /*delta*/) {
        const ecs::Entity entity = player();
        SoundBank* bank = world.IsAlive(entity) ? world.TryGet<SoundBank>(entity) : nullptr;
        if (bank == nullptr) {
            return;
        }
        const Player& state = world.Get<Player>(entity);
        const int wanted = state.dead ? 2 : (state.hurtTime > 0.0f ? 1 : 0);
        if (wanted == bank->mixState) {
            return;
        }
        bank->mixState = wanted;
        switch (wanted) {
        case 2:
            scene.Sounds().SetSnapshot({.buses = {0.7f, 0.5f, 0.7f, 1.0f}, .lowpass = 500.0f}, 1.5f);
            break;
        case 1:
            scene.Sounds().SetSnapshot({.buses = {1.0f, 0.8f, 1.0f, 1.0f}, .lowpass = 1800.0f}, 0.05f);
            break;
        default:
            scene.Sounds().SetSnapshot({}, 0.6f);
            break;
        }
    });
    systems.Add("Look", [&, player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            if (scene::Camera* camera = world.TryGet<scene::Camera>(entity)) {
                camera->aperture = settings.aperture;
                camera->focusDistance = settings.focusDistance;
            }
            LookAround(entity, scene, input, actions, settings.mouseSensitivity, static_cast<float>(delta.count()));
        }
    });
    fixed.Add("Zombies", [&scene, player](ecs::World& world, core::Seconds step) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdateZombies(scene, entity, static_cast<float>(step.count()));
        }
    });
    systems.Add("Weapons", [&, player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdateWeapons(scene, entity, input, actions, static_cast<float>(delta.count()));
        }
    });
    systems.Add("Zombie positions", [&scene](ecs::World& /*world*/, core::Seconds /*delta*/) { PlaceZombies(scene); });
    systems.Add("Pickups", [&scene, player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdatePickups(scene, entity, static_cast<float>(delta.count()));
        }
    });
    systems.Add("Looped sounds", [&scene, player, started = false](ecs::World& /*world*/, core::Seconds) mutable {
        if (!started) {
            started = true;
            StartLoopedSounds(scene, player());
        }
    });
    systems.Add("Lights", [&, player, handedOver = false](ecs::World& world, core::Seconds delta) mutable {
        const float seconds = static_cast<float>(delta.count());
        ApplyAtmosphere(scene, settings);
        world.Each<Alarm, ecs::LocalTransform>([seconds](ecs::Entity, const Alarm& alarm, ecs::LocalTransform& turned) {
            const float angle = glm::radians(alarm.degreesPerSecond) * seconds;
            turned.rotation = glm::normalize(glm::angleAxis(angle, math::kUp) * turned.rotation);
        });
        if (Player* state = world.TryGet<Player>(player())) {
            if (settings.startWithFlashlight && !std::exchange(handedOver, true)) {
                state->hasFlashlight = true;
                state->flashlightOn = true;
            }
            state->muzzleFlashTime = std::max(state->muzzleFlashTime - seconds, 0.0f);
            // The torch in the hand (ADR-0058): it trails the camera's turns and sways with each step, its
            // battery runs down while it is on, and it dims, buzzes and fails as the game says.
            const glm::vec2 look{state->yaw, state->pitch};
            const glm::vec2 turned = look - state->lastLook;
            state->lastLook = look;
            const glm::vec3 walk = (state->feet - state->previousFeet) / std::max(seconds, 1e-4f);
            SwayFlashlight(state->torch, turned.x, turned.y, glm::length(glm::vec2(walk.x, walk.z)), seconds);
            if (state->flashlightOn) {
                DrainFlashlight(state->torch, seconds, settings.flashlightBattery);
            }
            if (scene::Light* flashlight = world.TryGet<scene::Light>(state->flashlight)) {
                const float brightness = FlashlightBrightness(state->torch, scene.LightClock());
                flashlight->enabled = state->flashlightOn && brightness > 0.0f;
                flashlight->intensity = kFlashlightCandela * brightness;
            }
            if (ecs::LocalTransform* hand = world.TryGet<ecs::LocalTransform>(state->flashlight)) {
                const glm::vec2 aim = FlashlightAim(state->torch);
                const glm::vec3 straight = glm::normalize(math::kForward * kFlashlightAimDistance - kFlashlightHand);
                hand->rotation = glm::normalize(glm::angleAxis(aim.x, math::kUp) * glm::angleAxis(aim.y, math::kRight) *
                                                glm::quatLookAtRH(straight, math::kUp));
            }
            if (scene::Light* flash = world.TryGet<scene::Light>(state->muzzleFlash)) {
                flash->enabled = state->muzzleFlashTime > 0.0f;
            }
        }
    });
}

void FailFlashlight(Player& player, float seconds) {
    player.torch.fault = std::max(player.torch.fault, seconds);
}

} // namespace sandbox
