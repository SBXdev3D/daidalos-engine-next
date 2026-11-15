// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Factory.hpp"

#include <daidalos/animation/Animation.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/scene/Components.hpp>

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
#include <limits>
#include <map>
#include <optional>
#include <random>
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

// The factory is dark, lit by its lamps and the light they bounce (ADR-0040), in a thin haze that the lamps
// and the moon light (ADR-0051) and that swallows what is far away. It is exposed as a dim interior, at EV100 3
// (ADR-0043); without its darkness, Settings::darkness 0, this many stops brighter.
constexpr float kFactoryEv100 = 3.0f;
constexpr float kBrightStops = 4.0f;
// The eye adapts (ADR-0048) from this many stops below the factory's exposure, in its darkest corners, which
// stay dark, to this many above, under its lamps and in the moonlit room.
constexpr float kEyeOpensStops = 0.5f;
constexpr float kEyeClosesStops = 2.5f;
// The legacy sky's colors, in nits: as bright on screen at EV100 3 as they were before physical units.
constexpr float kSkyLuminance = 9.6f;
// The haze: a share of the light lost per meter, mostly scattered, a little more of the blue, and forwards.
// Thin, as indoor haze is (about 600 m of visibility): enough for the lamps' cones to show, not so much that
// the flashlight, at the eye and looked along, fills the view with its beam.
constexpr float kFogDensity = 0.0f;
constexpr glm::vec3 kFogAlbedo{0.8f, 0.82f, 0.85f};
constexpr float kFogAnisotropy = 0.1f;
// The steam of the leak: meters long and across, and its density at the heart.
constexpr float kSteamLength = 4.0f;
constexpr float kSteamWidth = 1.6f;
constexpr float kSteamDensity = 0.8f;
// The dust of the start room: where its box is from the player's start, its size, and its density.
constexpr glm::vec3 kDustOffset{0.0f, 0.0f, -3.5f};
constexpr glm::vec3 kDustSize{6.0f, 3.0f, 8.0f};
constexpr float kDustDensity = 0.1f;
// The moon painted on sky1 shines through the level's openings (ADR-0040). Its disk is centered at pixel
// (343.5, 147) of the 512-pixel front face, the legacy +Z: towards (0.344, 0.424, 1) in the cube, -Z in the
// engine, 22 degrees above the horizon. That low, it would light only walls that the player sees from
// behind: the sky is turned half a turn, which puts the moon behind the start and its light on the far
// wall of the big room ahead. The painted disk and its glow span 5 degrees; behind the clouds the light
// comes from a little less.
constexpr std::string_view kMoonSky = "sky1";
constexpr float kMoonSkyYaw = glm::pi<float>();
constexpr glm::vec3 kTowardsThePaintedMoon{0.344f, 0.424f, -1.0f};
// Light units (ADR-0043): each light is as bright as it was before them at a distance that matters, here at
// EV100 3, and falls off with the square of the distance from there.
constexpr glm::vec3 kMoonColor{0.88f, 1.0f, 1.26f};
constexpr float kMoonLux = 24.0f;
constexpr float kMoonAngularRadius = glm::radians(2.5f);
constexpr glm::vec3 kFlashlightColor{1.06f, 1.0f, 0.86f};
constexpr float kFlashlightCandela = 1090.0f;                // As bright as before 4 m away.
constexpr float kFlashlightRange = 32.0f;                    // Meters.
constexpr float kFlashlightInnerAngle = glm::radians(13.0f); // From the axis.
constexpr float kFlashlightOuterAngle = glm::radians(22.0f);
// The flashlight is in the left hand, the weapon in the right: from the eye, its shadows would all fall
// straight behind what casts them, out of sight. It aims at what the player looks at, a few meters ahead.
constexpr glm::vec3 kFlashlightHand{-0.3f, -0.15f, 0.0f};
constexpr float kFlashlightAimDistance = 8.0f;
// The light of a shot, after the legacy handgun's: 0.7 grey, 45 degrees, 0.7 m in front of the eye.
constexpr glm::vec3 kMuzzleFlashColor{1.11f, 1.0f, 0.72f};
constexpr float kMuzzleFlashCandela = 150.0f; // As bright as before 2 m away.
constexpr float kMuzzleFlashRange = 7.0f;
// Each alarm turns two red spots, back to back, 0.7 m under it and 25 degrees below the horizon, as the
// legacy CAlarmEntity did.
constexpr glm::vec3 kAlarmColor{1.0f, 0.0f, 0.0f};
constexpr float kAlarmCandela = 965.0f; // As bright as before 8 m away.
constexpr float kAlarmRange = 28.0f;
constexpr float kAlarmAngle = glm::radians(20.0f);
constexpr float kAlarmTightness = 0.8f; // Fraction of the angle at full light.
constexpr float kAlarmTilt = glm::radians(25.0f);
constexpr float kAlarmOffset = 0.7f; // Meters below the alarm.
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

/// The name of a node is "kind;key=value;key=value".
struct NodeDescription {
    std::string_view kind;
    std::map<std::string_view, std::string_view> attributes;
};

NodeDescription Parse(std::string_view name) {
    NodeDescription description;
    std::size_t end = name.find(';');
    description.kind = name.substr(0, end);
    while (end != std::string_view::npos) {
        const std::size_t start = end + 1;
        end = name.find(';', start);
        const std::string_view field = name.substr(start, end == std::string_view::npos ? end : end - start);
        const std::size_t equal = field.find('=');
        if (equal != std::string_view::npos) {
            description.attributes.emplace(field.substr(0, equal), field.substr(equal + 1));
        }
    }
    return description;
}

template <class T>
T Number(const NodeDescription& description, std::string_view key, T fallback) {
    const auto found = description.attributes.find(key);
    if (found == description.attributes.end()) {
        return fallback;
    }
    T value = fallback;
    const std::string_view text = found->second;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

/// A color "r:g:b:a", as the legacy level writes it. Alpha is ignored.
glm::vec3 Color(const NodeDescription& description, std::string_view key, const glm::vec3& fallback) {
    const auto found = description.attributes.find(key);
    if (found == description.attributes.end()) {
        return fallback;
    }
    glm::vec3 color = fallback;
    std::string_view text = found->second;
    for (glm::length_t channel = 0; channel < 3 && !text.empty(); ++channel) {
        const std::size_t end = text.find(':');
        std::from_chars(text.data(), text.data() + text.substr(0, end).size(), color[channel]);
        text = (end == std::string_view::npos) ? std::string_view{} : text.substr(end + 1);
    }
    return color;
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

/// The effects of the legacy game's particle templates, in meters and seconds: lengths were 0.35 m per
/// unit and times milliseconds. Every one uses a cell of the author's atlas, a grid of 4 by 4: cell 0 is a
/// soft puff, 1 a hard white disc, 3 a rough bright burst.
ParticleEffects MakeParticleEffects(scene::Scene& scene, const std::string& directory) {
    ParticleEffects effects;
    const core::Result<rhi::TextureHandle> atlas =
        scene.Assets().LoadTexture(directory.empty() ? "particles.png" : directory + "/particles.png");
    if (!atlas) {
        DAI_LOG_WARNING("sandbox", "No particles: {}", core::Format(atlas.error()));
        return effects;
    }
    const auto create = [&](renderer::ParticleEffectDesc desc) {
        desc.texture = *atlas;
        desc.columns = 4;
        core::Result<renderer::ParticleEffectHandle> effect = scene.Assets().CreateParticleEffect(desc);
        if (!effect) {
            DAI_LOG_WARNING("sandbox", "A particle effect is missing: {}", core::Format(effect.error()));
        }
        return effect.value_or(renderer::ParticleEffectHandle{});
    };
    // The atlas's puffs are grey already: colors above it tint them.
    const glm::vec4 green{0.5f, 1.0f, 0.5f, 0.2f};
    effects.smokeLeak = create({.cell = 0,
                                .startColor = {1.0f, 1.0f, 1.0f, 0.15f},
                                .endColor = {1.0f, 1.0f, 1.0f, 0.0f},
                                .lifetime = {2.0f, 6.0f},
                                .size = {0.5f, 0.8f},
                                .growth = 4.0f,
                                .spawnRadius = 0.1f,
                                .direction = {0.0f, 0.0f, -1.0f},
                                .spread = 0.35f,
                                .speed = {0.3f, 0.6f},
                                .rotationSpeed = 0.2f,
                                .softness = 0.5f});
    effects.greenSmoke = create({.cell = 0,
                                 .startColor = green,
                                 .endColor = {green.r, green.g, green.b, 0.0f},
                                 .lifetime = {0.5f, 5.0f},
                                 .size = {1.4f, 1.4f},
                                 .growth = 2.0f,
                                 .spawnBox = {3.5f, 0.0f, 1.75f},
                                 .speed = {0.7f, 1.05f},
                                 .rotationSpeed = 0.2f,
                                 .softness = 0.5f});
    effects.greenFog = create({.cell = 0,
                               .startColor = {green.r, green.g, green.b, 0.4f},
                               .endColor = {green.r, green.g, green.b, 0.0f},
                               .lifetime = {0.5f, 4.0f},
                               .size = {2.1f, 2.1f},
                               .growth = 1.33f,
                               .spawnBox = {3.5f, 0.0f, 3.5f},
                               .speed = {0.035f, 0.035f},
                               .rotationSpeed = 0.1f,
                               .softness = 0.8f});
    effects.blood = create({.cell = 0,
                            .startColor = {0.6f, 0.08f, 0.08f, 0.8f},
                            .endColor = {0.4f, 0.05f, 0.05f, 0.0f},
                            .collides = true,
                            .bounce = 0.1f,
                            .lifetime = {0.3f, 0.6f},
                            .size = {0.04f, 0.07f},
                            .growth = 2.0f,
                            .spawnRadius = 0.035f,
                            .spread = 1.2f,
                            .speed = {0.7f, 3.5f},
                            .gravity = 1.0f,
                            .drag = 0.2f,
                            .softness = 0.05f});
    effects.sparks = create({.cell = 1,
                             .startColor = {3.0f, 2.6f, 1.2f, 1.0f},
                             .endColor = {3.0f, 1.5f, 0.5f, 0.0f},
                             .additive = true,
                             .lit = false,
                             .collides = true,
                             .bounce = 0.4f,
                             .lifetime = {0.15f, 0.4f},
                             .size = {0.03f, 0.05f},
                             .spread = 1.2f,
                             .speed = {1.5f, 4.0f},
                             .gravity = 1.0f,
                             .softness = 0.02f});
    effects.gunSmoke = create({.cell = 0,
                               .startColor = {1.0f, 1.0f, 1.0f, 0.12f},
                               .endColor = {1.0f, 1.0f, 1.0f, 0.0f},
                               .lifetime = {1.5f, 2.5f},
                               .size = {0.04f, 0.06f},
                               .growth = 3.0f,
                               .spawnRadius = 0.035f,
                               .speed = {0.14f, 0.28f},
                               .drag = 0.2f,
                               .rotationSpeed = 0.5f,
                               .softness = 0.1f});
    effects.muzzleFlash = create({.cell = 3,
                                  .startColor = {2.5f, 2.2f, 1.0f, 1.0f},
                                  .endColor = {2.5f, 1.6f, 0.6f, 0.0f},
                                  .additive = true,
                                  .lit = false,
                                  .lifetime = {0.06f, 0.08f},
                                  .size = {0.05f, 0.08f},
                                  .spawnRadius = 0.02f,
                                  .softness = 0.02f});
    return effects;
}

/// Steam around a leak, which blows along -Z as its smoke does (ADR-0051): a box of fog that starts at the
/// leak and widens away from it, whose noise drifts with the smoke, lit by the lamps around.
void AddSteam(ecs::World& world, ecs::Entity leak) {
    const ecs::Entity steam = world.Create();
    ecs::LocalTransform transform;
    transform.translation = {0.0f, 0.1f, -kSteamLength * 0.5f + 0.2f};
    transform.scale = {kSteamWidth, kSteamWidth, kSteamLength};
    world.Add<ecs::LocalTransform>(steam, transform);
    world.Add<scene::FogVolume>(steam, scene::FogVolume{.density = kSteamDensity,
                                                        .albedo = glm::vec3(0.95f),
                                                        .edge = 0.8f,
                                                        .noise = 0.8f,
                                                        .noiseFrequency = 1.5f,
                                                        .noiseVelocity = {0.0f, 0.1f, -0.5f}});
    [[maybe_unused]] const core::Result<void> attached = world.SetParent(steam, leak);
}

/// Dust in the start room (ADR-0051): a box of thin fog around the player's start and the lamp ahead,
/// whose noise drifts slowly, so that swirls move through the lamp's cone.
void AddDust(ecs::World& world, const glm::vec3& start) {
    const ecs::Entity dust = world.Create();
    ecs::LocalTransform transform;
    transform.translation = start + kDustOffset;
    transform.scale = kDustSize;
    world.Add<ecs::LocalTransform>(dust, transform);
    world.Add<scene::FogVolume>(dust, scene::FogVolume{.density = kDustDensity,
                                                       .albedo = {0.85f, 0.8f, 0.7f},
                                                       .edge = 0.3f,
                                                       .noise = 0.7f,
                                                       .noiseFrequency = 0.8f,
                                                       .noiseVelocity = {0.08f, 0.03f, 0.0f}});
}

/// A level emitter, by the name of its legacy template, at the rate the legacy game emitted.
void AddParticleEmitter(ecs::World& world, ecs::Entity entity, const NodeDescription& node,
                        const ParticleEffects& effects) {
    const auto found = node.attributes.find("effect");
    const std::string_view name = found == node.attributes.end() ? std::string_view{} : found->second;
    if (name == "smokeLeakEmitter") {
        world.Add<scene::ParticleEmitter>(entity, effects.smokeLeak, 12.0f);
        AddSteam(world, entity);
    } else if (name == "smokeGreenEmitter") {
        world.Add<scene::ParticleEmitter>(entity, effects.greenSmoke, 100.0f);
    } else if (name == "fogGreenEmitter") {
        world.Add<scene::ParticleEmitter>(entity, effects.greenFog, 50.0f);
    } else {
        DAI_LOG_WARNING("sandbox", "Unknown particle effect '{}'", name);
    }
}

void AddAlarmLights(ecs::World& world, ecs::Entity alarm, const NodeDescription& node) {
    world.Add<Alarm>(alarm, Number(node, "rotation_speed", kAlarmSpeed));
    const scene::Light light{.type = renderer::LightType::Spot,
                             .color = kAlarmColor,
                             .intensity = kAlarmCandela,
                             .range = kAlarmRange,
                             .innerAngle = kAlarmAngle * kAlarmTightness,
                             .outerAngle = kAlarmAngle};
    for (const float yaw : {0.0f, glm::pi<float>()}) {
        AddLight(world, alarm, -math::kUp * kAlarmOffset, math::FromYawPitchRoll(yaw, -kAlarmTilt, 0.0f), light);
    }
}

/// A value "x,y,z".
glm::vec3 Vector(const NodeDescription& description, std::string_view key) {
    glm::vec3 value{0.0f};
    const auto found = description.attributes.find(key);
    if (found == description.attributes.end()) {
        return value;
    }
    const char* cursor = found->second.data();
    const char* const end = cursor + found->second.size();
    for (int axis = 0; axis < 3 && cursor < end; ++axis) {
        cursor = std::from_chars(cursor, end, value[axis]).ptr + 1; // Past the comma.
    }
    return value;
}

/// A box of fog that the level places (ADR-0051): "fog;density=0.1;falloff=0.4;color=r:g:b:a;edge=0.3;
/// noise=0.5;noise_frequency=1;noise_velocity=x,y,z". The node's transform is the box, the cube from -0.5 to
/// 0.5 scaled to its size. With a falloff in meters, the fog lies on the ground: it halves every falloff
/// going up from the box's bottom, which belongs on the floor.
void AddFog(ecs::World& world, ecs::Entity entity, const NodeDescription& node) {
    world.Add<scene::FogVolume>(entity, scene::FogVolume{.density = Number(node, "density", 0.1f),
                                                         .albedo = Color(node, "color", kFogAlbedo),
                                                         .edge = Number(node, "edge", 0.3f),
                                                         .falloffHeight = Number(node, "falloff", 0.0f),
                                                         .noise = Number(node, "noise", 0.0f),
                                                         .noiseFrequency = Number(node, "noise_frequency", 1.0f),
                                                         .noiseVelocity = Vector(node, "noise_velocity")});
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
        player.hasFlashlight = true;
        player.flashlightOn = true;
        Say(player, "Flashlight: press F");
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
            scene.Sounds().Play(bank->steps[bank->lastStep]);
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

/// The sky that the level's openings show: six faces next to the level, named as the legacy engine named
/// them, in the order of a cube's faces (ADR-0036). A sky with a moon also lights the level from it.
core::Result<void> LoadSky(scene::Scene& scene, const std::string& directory, std::string_view name) {
    const std::string prefix = (directory.empty() ? std::string("Sky/") : directory + "/Sky/") + std::string(name);
    const std::array<std::string, 6> faces{prefix + "_right.png", prefix + "_left.png",  prefix + "_up.png",
                                           prefix + "_down.png",  prefix + "_front.png", prefix + "_back.png"};
    DAI_TRY_ASSIGN(rhi::TextureHandle sky, scene.Assets().LoadCubeTexture(faces));
    renderer::Atmosphere atmosphere = scene.Atmosphere();
    atmosphere.sky = sky;
    atmosphere.skyIntensity = kSkyLuminance;
    if (name == kMoonSky) {
        atmosphere.skyYaw = kMoonSkyYaw;
        // A light from where the moon is painted, turned with the sky, which shines down its local -Z.
        ecs::World& world = scene.World();
        const ecs::Entity moon = world.Create();
        const glm::vec3 towardsTheMoon =
            glm::angleAxis(kMoonSkyYaw, math::kUp) * glm::normalize(kTowardsThePaintedMoon);
        ecs::LocalTransform transform;
        transform.rotation = glm::quatLookAtRH(-towardsTheMoon, math::kUp);
        world.Add<ecs::LocalTransform>(moon, transform);
        world.Add<scene::Light>(moon, scene::Light{.type = renderer::LightType::Directional,
                                                   .color = kMoonColor,
                                                   .intensity = kMoonLux,
                                                   .angularRadius = kMoonAngularRadius});
    }
    scene.SetAtmosphere(atmosphere);
    return {};
}

/// The factory's darkness and fog, which follow the settings. The sky stays as the level set it.
void ApplyAtmosphere(scene::Scene& scene, const Settings& settings) {
    const float darkness = std::clamp(settings.darkness, 0.0f, 1.0f);
    // Brightness is seen in stops: halfway is half the stops.
    const float ev100 = kFactoryEv100 - ((1.0f - darkness) * kBrightStops);
    scene.SetAtmosphere({.fog = {.density = kFogDensity * darkness, .albedo = kFogAlbedo, .anisotropy = kFogAnisotropy},
                         .sky = scene.Atmosphere().sky,
                         .skyIntensity = scene.Atmosphere().skyIntensity,
                         .skyYaw = scene.Atmosphere().skyYaw,
                         .ev100 = ev100,
                         .autoExposure = true,
                         .ev100Min = ev100 - kEyeOpensStops,
                         .ev100Max = ev100 + kEyeClosesStops});
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

/// Puts a model at a level node, as its child.
core::Result<ecs::Entity> PlaceModel(scene::Scene& scene, ecs::Entity node, const std::string& path) {
    DAI_TRY_ASSIGN(const scene::Model* model, scene.Assets().LoadModel(path));
    ecs::World& world = scene.World();
    core::Result<ecs::Entity> instance = scene::Instantiate(world, *model);
    if (!instance) {
        return instance;
    }
    DAI_TRY(world.SetParent(*instance, node));
    return instance;
}

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

core::Result<ecs::Entity> BuildLevel(scene::Scene& scene, const scene::Model& model, std::string_view levelPath) {
    ecs::World& world = scene.World();
    core::Result<ecs::Entity> root = scene::Instantiate(world, model);
    if (!root) {
        return root;
    }
    physics::PhysicsWorld& physics = scene.Physics();

    // What the game needs from the geometry: the level to walk on, and the size of the doors.
    std::map<std::string_view, const scene::Model::Node*> modelNodes;
    for (const scene::Model::Node& node : model.nodes) {
        modelNodes.emplace(node.name, &node);
        if (node.name != "level") {
            continue;
        }
        for (const std::uint32_t index : node.parts) {
            const scene::Model::Part& part = model.parts[index];
            DAI_TRY(physics.AddStaticMesh(part.positions, part.indices, node.local));
        }
    }

    ecs::Entity playerEntity = world.Create();
    glm::vec3 eye{0.0f, kEyeHeight, 3.0f};
    Player& newPlayer = world.Add<Player>(playerEntity);
    world.Add<scene::Camera>(playerEntity);
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
    // Models that the level places, such as zombies, are found next to it, and so are the weapons.
    const std::string directory = std::filesystem::path(levelPath).parent_path().generic_string();
    const physics::BodyId playerBody = physics.CharacterBody(world.Get<Player>(playerEntity).character);

    std::vector<int> lockedDoors;
    std::vector<std::pair<ecs::Entity, std::string>> nodes;
    world.Each<scene::NodeName>(
        [&nodes](ecs::Entity entity, const scene::NodeName& name) { nodes.emplace_back(entity, name.value); });
    for (const auto& [entity, name] : nodes) {
        const NodeDescription node = Parse(name);
        if (node.kind == "card_reader") {
            lockedDoors.push_back(Number(node, "id", 0));
        }
    }
    const ParticleEffects& effects = world.Add<ParticleEffects>(playerEntity, MakeParticleEffects(scene, directory));
    // After the level's nodes are listed: the weapons have named nodes too, which are not the level's.
    SetUpCombat(scene, playerEntity, directory);
    std::minstd_rand random{std::random_device{}()};
    for (const auto& [entity, name] : nodes) {
        const NodeDescription node = Parse(name);
        const glm::vec3 position = world.Get<ecs::LocalTransform>(entity).translation;
        if (const auto file = node.attributes.find("model"); file != node.attributes.end()) {
            const std::string path =
                directory.empty() ? std::string(file->second) : directory + "/" + std::string(file->second);
            DAI_TRY_ASSIGN(ecs::Entity instance, PlaceModel(scene, entity, path));
            if (node.kind == "zombie") {
                const auto patrol = node.attributes.find("patrol");
                AddZombie(scene, entity, instance, patrol == node.attributes.end() ? "" : patrol->second);
                ++world.Get<Player>(playerEntity).zombieCount;
            } else if (node.kind == "fan") {
                PlayForever(world, instance, random);
            }
        }
        if (node.kind == "player") {
            AddDust(world, position);
            eye = position;
            if (const std::optional<physics::RayHit> floor = physics.CastRay(position, -math::kUp, 20.0f, playerBody)) {
                eye = floor->position + math::kUp * kEyeHeight;
            }
        } else if (node.kind == "alarm") {
            world.Add<LoopedSound>(entity, bank.alarm, 0.5f, 1.4f, 35.0f);
            AddAlarmLights(world, entity, node);
        } else if (const auto sky = node.attributes.find("name"); node.kind == "sky" && sky != node.attributes.end()) {
            DAI_TRY(LoadSky(scene, directory, sky->second));
        } else if (node.kind == "particles") {
            AddParticleEmitter(world, entity, node, effects);
        } else if (node.kind == "fog") {
            AddFog(world, entity, node);
        } else if (node.kind == "fan") {
            world.Add<LoopedSound>(entity, bank.fan, 0.7f, 1.0f, 20.0f);
        } else if (node.kind == "ladder") {
            world.Add<Ladder>(entity, Vector(node, "min"), Vector(node, "max"));
        } else if (const std::optional<PickupKind> pickup = PickupOf(node.kind)) {
            world.Add<Pickup>(entity, *pickup);
        } else if (node.kind == "door") {
            Door door;
            door.id = Number(node, "id", 0);
            door.closedPosition = position;
            door.radius = Number(node, "radius", 1.0f) < 0.0f ? -1.0f : kDoorRadius;
            const auto closable = node.attributes.find("closable");
            door.closable = closable == node.attributes.end() || closable->second != "false";
            door.locked = std::ranges::find(lockedDoors, door.id) != lockedDoors.end();
            world.Add<Door>(entity, door);

            if (const auto found = modelNodes.find(name); found != modelNodes.end()) {
                glm::vec3 low(std::numeric_limits<float>::max());
                glm::vec3 high(std::numeric_limits<float>::lowest());
                for (const std::uint32_t index : found->second->parts) {
                    for (const glm::vec3& vertex : model.parts[index].positions) {
                        low = glm::min(low, vertex);
                        high = glm::max(high, vertex);
                    }
                }
                if (low.x <= high.x) {
                    math::Transform transform;
                    transform.translation = position;
                    const glm::vec3 halfExtents = glm::max((high - low) * 0.5f, glm::vec3(0.05f));
                    DAI_TRY_ASSIGN(physics::BodyId box,
                                   physics.AddKinematicBox(halfExtents, (low + high) * 0.5f, transform));
                    world.Add<scene::KinematicBody>(entity, box);
                }
            }
        }
    }
    ecs::LocalTransform start;
    start.translation = eye;
    world.Add<ecs::LocalTransform>(playerEntity, start);
    Teleport(scene, playerEntity, eye);
    scene.SetActiveCamera(playerEntity);
    return playerEntity;
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
    });

    ecs::Schedule& systems = scene.FrameSystems();
    systems.Add("Look", [&, player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
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
            if (scene::Light* flashlight = world.TryGet<scene::Light>(state->flashlight)) {
                flashlight->enabled = state->flashlightOn;
            }
            if (scene::Light* flash = world.TryGet<scene::Light>(state->muzzleFlash)) {
                flash->enabled = state->muzzleFlashTime > 0.0f;
            }
        }
    });
}

} // namespace sandbox
