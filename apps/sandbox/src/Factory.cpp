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

/// Where the level's effect files are: the effects of the legacy game's particle templates, in meters and
/// seconds, in files since ADR-0053.
std::string EffectPath(const std::string& directory, std::string_view name) {
    return std::format("{}Effects/{}.effect.json", directory.empty() ? "" : directory + "/", name);
}

/// Where the level's ribbon files are (ADR-0053).
std::string RibbonPath(const std::string& directory, std::string_view name) {
    return std::format("{}Effects/{}.ribbon.json", directory.empty() ? "" : directory + "/", name);
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
            .casings = load("shell_casings")};
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

/// The entity of the level node of this name, if any.
std::optional<ecs::Entity> NodeEntity(std::span<const std::pair<ecs::Entity, std::string>> nodes,
                                      std::string_view name) {
    const auto named = std::ranges::find(nodes, name, [](const auto& pair) -> std::string_view { return pair.second; });
    return named != nodes.end() ? std::optional<ecs::Entity>(named->first) : std::nullopt;
}

/// The triangles of a model's node, all its parts together, in the node's frame.
std::shared_ptr<const scene::MeshSurface> SurfaceOf(const scene::Model& model, const scene::Model::Node& node) {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    for (const std::uint32_t index : node.parts) {
        const scene::Model::Part& part = model.parts[index];
        const auto first = static_cast<std::uint32_t>(positions.size());
        positions.insert(positions.end(), part.positions.begin(), part.positions.end());
        for (const std::uint32_t vertex : part.indices) {
            indices.push_back(first + vertex);
        }
    }
    return scene::MakeMeshSurface(positions, indices);
}

/// A level emitter, by the name of its effect file, emitting as the file says. With "surface=<node>", it
/// emits on that node's triangles instead (ADR-0053): what burns, what crumbles; with "burst=<count>:<seconds>",
/// only bursts of that many, that often.
void AddParticleEmitter(scene::Scene& scene, ecs::Entity entity, const NodeDescription& node,
                        const std::string& directory, const scene::Model& model,
                        std::span<const std::pair<ecs::Entity, std::string>> nodes) {
    const auto found = node.attributes.find("effect");
    const std::string_view name = found == node.attributes.end() ? std::string_view{} : found->second;
    core::Result<scene::ParticleEffectAsset> effect = scene.Assets().LoadParticleEffect(EffectPath(directory, name));
    if (!effect) {
        DAI_LOG_WARNING("sandbox", "Particle emitter without its effect: {}", core::Format(effect.error()));
        return;
    }
    scene::ParticleEmitter emitter{.effect = effect->effect, .emission = effect->emission};
    // "burst=<count>:<interval>" emits only that burst, again and again: impacts and explosions shown on
    // their own.
    if (const auto burst = node.attributes.find("burst"); burst != node.attributes.end()) {
        const std::string_view text = burst->second;
        const std::size_t colon = text.find(':');
        scene::ParticleBurst repeated{.count = 1, .interval = 1.0f};
        std::from_chars(text.data(), text.data() + std::min(colon, text.size()), repeated.count);
        if (colon != std::string_view::npos) {
            std::from_chars(text.data() + colon + 1, text.data() + text.size(), repeated.interval);
        }
        emitter.emission.rate = 0.0f;
        emitter.emission.ratePerMeter = 0.0f;
        emitter.emission.bursts = {repeated};
    }
    if (const auto surface = node.attributes.find("surface"); surface != node.attributes.end()) {
        const auto modelNode = std::ranges::find(model.nodes, surface->second, &scene::Model::Node::name);
        const std::optional<ecs::Entity> target = NodeEntity(nodes, surface->second);
        if (modelNode == model.nodes.end() || !target) {
            DAI_LOG_WARNING("sandbox", "Particle emitter '{}': no node '{}' to emit on", name, surface->second);
            return;
        }
        emitter.surface = SurfaceOf(model, *modelNode);
        if (emitter.surface == nullptr) {
            DAI_LOG_WARNING("sandbox", "Particle emitter '{}': node '{}' has no triangles", name, surface->second);
            return;
        }
        // On the node that has the triangles, which moves them.
        scene.World().Add<scene::ParticleEmitter>(*target, std::move(emitter));
        return;
    }
    scene.World().Add<scene::ParticleEmitter>(entity, std::move(emitter));
    if (name == "smoke_leak") {
        AddSteam(scene.World(), entity);
    }
}

/// A level ribbon, by the name of its file, to the node its target names, if any.
void AddRibbonEmitter(scene::Scene& scene, ecs::Entity entity, const NodeDescription& node,
                      const std::string& directory, std::span<const std::pair<ecs::Entity, std::string>> nodes) {
    const auto found = node.attributes.find("effect");
    const std::string_view name = found == node.attributes.end() ? std::string_view{} : found->second;
    core::Result<std::shared_ptr<const scene::RibbonEffect>> effect =
        scene.Assets().LoadRibbonEffect(RibbonPath(directory, name));
    if (!effect) {
        DAI_LOG_WARNING("sandbox", "Ribbon without its effect: {}", core::Format(effect.error()));
        return;
    }
    scene::RibbonEmitter ribbon{.effect = *effect};
    if (const auto target = node.attributes.find("target"); target != node.attributes.end()) {
        const auto named =
            std::ranges::find(nodes, target->second, [](const auto& pair) -> std::string_view { return pair.second; });
        if (named != nodes.end()) {
            ribbon.target = named->first;
        } else {
            DAI_LOG_WARNING("sandbox", "Ribbon '{}': no node '{}' to reach", name, target->second);
        }
    }
    scene.World().Add<scene::RibbonEmitter>(entity, std::move(ribbon));
}

/// A level box where it rains or snows (ADR-0053): "weather;effect=rain;rate=60;reach=15". The node's
/// transform is the box, the cube from -0.5 to 0.5.
void AddWeather(scene::Scene& scene, ecs::Entity entity, const NodeDescription& node, const std::string& directory) {
    const auto found = node.attributes.find("effect");
    const std::string_view name = found == node.attributes.end() ? std::string_view{} : found->second;
    core::Result<scene::ParticleEffectAsset> effect = scene.Assets().LoadParticleEffect(EffectPath(directory, name));
    if (!effect) {
        DAI_LOG_WARNING("sandbox", "Weather without its effect: {}", core::Format(effect.error()));
        return;
    }
    scene.World().Add<scene::WeatherZone>(entity, scene::WeatherZone{.effect = effect->effect,
                                                                     .rate = Number(node, "rate", 50.0f),
                                                                     .reach = Number(node, "reach", 15.0f)});
}

/// A level storm's lightning (ADR-0053): "lightning;bolt=lightning;light=<node>;flash=30;interval=6:15;
/// duration=0.5", striking in the node's box, flashing the light of the named node.
void AddLightning(scene::Scene& scene, ecs::Entity entity, const NodeDescription& node, const std::string& directory,
                  std::span<const std::pair<ecs::Entity, std::string>> nodes) {
    scene::Lightning lightning;
    const auto bolt = node.attributes.find("bolt");
    core::Result<std::shared_ptr<const scene::RibbonEffect>> ribbon = scene.Assets().LoadRibbonEffect(
        RibbonPath(directory, bolt == node.attributes.end() ? std::string_view("lightning") : bolt->second));
    if (ribbon) {
        lightning.bolt = *ribbon;
    } else {
        DAI_LOG_WARNING("sandbox", "Lightning without its bolt: {}", core::Format(ribbon.error()));
    }
    if (const auto light = node.attributes.find("light"); light != node.attributes.end()) {
        if (const std::optional<ecs::Entity> found = NodeEntity(nodes, light->second)) {
            lightning.light = *found;
        } else {
            DAI_LOG_WARNING("sandbox", "Lightning: no node '{}' to flash", light->second);
        }
    }
    lightning.flash = Number(node, "flash", lightning.flash);
    lightning.duration = Number(node, "duration", lightning.duration);
    if (const auto interval = node.attributes.find("interval"); interval != node.attributes.end()) {
        const std::string_view text = interval->second;
        const std::size_t colon = text.find(':');
        std::from_chars(text.data(), text.data() + std::min(colon, text.size()), lightning.interval.x);
        lightning.interval.y = lightning.interval.x;
        if (colon != std::string_view::npos) {
            std::from_chars(text.data() + colon + 1, text.data() + text.size(), lightning.interval.y);
        }
    }
    scene.World().Add<scene::Lightning>(entity, std::move(lightning));
}

glm::vec3 Vector(const NodeDescription& description, std::string_view key);

/// A level point that pulls particles and turns them around its +Y (ADR-0053): "attractor;effect=flies;
/// strength=2;vortex=3;radius=1.5", moving only that effect's particles if it names one.
void AddAttractor(scene::Scene& scene, ecs::Entity entity, const NodeDescription& node, const std::string& directory) {
    scene::ParticleAttractor attractor{.strength = Number(node, "strength", 1.0f),
                                       .vortex = Number(node, "vortex", 0.0f),
                                       .radius = Number(node, "radius", 0.0f)};
    if (const auto found = node.attributes.find("effect"); found != node.attributes.end()) {
        core::Result<scene::ParticleEffectAsset> effect =
            scene.Assets().LoadParticleEffect(EffectPath(directory, found->second));
        if (!effect) {
            DAI_LOG_WARNING("sandbox", "Attractor without its effect: {}", core::Format(effect.error()));
            return;
        }
        attractor.effect = effect->effect;
    }
    scene.World().Add<scene::ParticleAttractor>(entity, attractor);
}

/// A level box of wind (ADR-0053): "wind;velocity=x,y,z;gust=0.5;gust_frequency=0.3;edge=0.3", its velocity
/// in the node's frame; the node's transform is the box.
void AddWind(ecs::World& world, ecs::Entity entity, const NodeDescription& node) {
    world.Add<scene::WindVolume>(entity, scene::WindVolume{.velocity = Vector(node, "velocity"),
                                                           .gust = Number(node, "gust", 0.0f),
                                                           .gustFrequency = Number(node, "gust_frequency", 0.2f),
                                                           .edge = Number(node, "edge", 0.3f)});
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
    world.Add<ParticleEffects>(playerEntity, LoadParticleEffects(scene, directory));
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
            AddParticleEmitter(scene, entity, node, directory, model, nodes);
        } else if (node.kind == "attractor") {
            AddAttractor(scene, entity, node, directory);
        } else if (node.kind == "wind") {
            AddWind(world, entity, node);
        } else if (node.kind == "weather") {
            AddWeather(scene, entity, node, directory);
        } else if (node.kind == "lightning") {
            AddLightning(scene, entity, node, directory, nodes);
        } else if (node.kind == "ribbon") {
            AddRibbonEmitter(scene, entity, node, directory, nodes);
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
