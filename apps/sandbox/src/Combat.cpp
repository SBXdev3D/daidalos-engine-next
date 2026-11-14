// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Combat.hpp"

#include <daidalos/animation/Animation.hpp>
#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <random>
#include <utility>
#include <vector>

using namespace daidalos;

namespace sandbox {

namespace {

// The numbers of the original demo, whose units were 0.35 m, unless said otherwise.
constexpr float kLegacyUnit = 0.35f;

struct WeaponSpec {
    const char* model;
    const char* fireSound;
    const char* reloadSound;
    int magazine;
    int maxReserve;
    int damage;
    int pellets;
    /// Largest change to each axis of the aim, as the legacy game spread its shots.
    float spread;
    /// Seconds between two shots, and of a reload. The original reloaded the handgun in 2 s.
    float fireDelay;
    float reloadTime;
    /// Where the weapon is held, in the camera's space, and its size.
    glm::vec3 offset;
    float scale;
};

constexpr std::array<WeaponSpec, kWeaponKinds> kWeapons{{
    {"handgun.gltf", "handgun_fire.wav", "handgun_reload.wav", 12, 108, 10, 1, 0.025f, 0.35f, 1.5f,
     glm::vec3(0.05f, -0.1f, -0.2f), 0.36f},
    {"shotgun.gltf", "shotgun_fire.wav", "shotgun_reload.wav", 7, 43, 12, 5, 0.0875f, 1.2f, 2.8f,
     glm::vec3(0.035f, -0.135f, -0.24f), 0.72f},
}};

constexpr float kWeaponRange = 100.0f * kLegacyUnit;
constexpr float kSwitchTime = 0.4f;    // Seconds for the new weapon to come up.
constexpr float kWeaponDrop = 0.6f;    // Meters below its place, where a weapon is out of view.
constexpr float kGunshotAlert = 20.0f; // Meters: zombies this close hear a shot and come.

constexpr float kZombieRadius = 0.3f;
constexpr float kZombieHeight = 1.6f;
constexpr float kZombieEye = 1.45f;
constexpr float kZombieSight = 35.0f * kLegacyUnit;
constexpr float kZombieViewAngle = glm::radians(60.0f); // From the direction it faces.
// Faster than the original's 0.7 m/s, for a faster game (docs/DEMO.md). The walk clip speeds up with it.
constexpr float kZombieSpeed = 1.3f;
constexpr float kLegacyZombieSpeed = 2.0f * kLegacyUnit;
constexpr float kZombieTurnSpeed = glm::radians(180.0f);
constexpr float kZombieReach = 4.0f * kLegacyUnit;
constexpr float kZombieReachMargin = 0.4f; // A strike still lands on a player stepping back this far.
constexpr int kZombieDamage = 12;
// Seconds per strike: the length of its clip, as the original waited for the clip to end.
constexpr float kZombieSwing = 1.5f;
constexpr float kZombieStrikeAt = 0.55f; // Of the swing, when it lands.
constexpr float kZombieSearch = 9.0f;    // Seconds it looks for a player it lost.
constexpr float kZombieWait = 6.0f;      // Seconds at each patrol point.
constexpr float kZombieFade = 0.25f;
// A zombie that walks at less than this share of its speed for that long is against a wall: with no
// path finding, it gives up on where it was going.
constexpr float kStuckSpeed = 0.3f;
constexpr float kStuckTime = 1.5f;
constexpr float kGravity = 7.0f;
constexpr float kFloorSearch = 0.5f; // Meters above a zombie where its floor is looked for.
constexpr float kFloorDrop = 20.0f;  // Meters below it.

constexpr float kHurtFlash = 0.35f; // Seconds.
constexpr float kDeadMessageTime = 3600.0f;

enum class ZombieState : std::uint8_t { Waiting, Patrolling, Chasing, Attacking, Dying };

/// ECS component on a zombie's level node, whose child is the model.
struct Zombie {
    ecs::Entity model;
    physics::CharacterHandle character;
    physics::BodyId body;
    ZombieState state = ZombieState::Waiting;
    int health = 50;
    std::vector<glm::vec3> patrol;
    std::size_t nextPoint = 0;
    /// Seconds left of a wait, or of a search.
    float timer = 0.0f;
    float swing = 0.0f;
    bool struck = false;
    glm::vec3 lastSeen{0.0f};
    float yaw = 0.0f;
    glm::vec3 previousFeet{0.0f};
    glm::vec3 feet{0.0f};
    float verticalSpeed = 0.0f;
    float groanTime = 0.0f;
    /// Seconds spent walking without getting anywhere.
    float stuck = 0.0f;
};

/// ECS component on the player: the sounds of the fight, and the weapons held in view.
struct Arsenal {
    std::array<audio::ClipHandle, kWeaponKinds> fire;
    std::array<audio::ClipHandle, kWeaponKinds> reload;
    audio::ClipHandle change;
    audio::ClipHandle hurt;
    audio::ClipHandle die;
    audio::ClipHandle bodyFall;
    audio::ClipHandle moan;
    std::array<audio::ClipHandle, 3> groans;
    std::array<ecs::Entity, kWeaponKinds> models;
    std::minstd_rand random{std::random_device{}()};
};

float Random(std::minstd_rand& random, float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(random);
}

float YawOf(const glm::vec3& direction) {
    return std::atan2(-direction.x, -direction.z);
}

glm::vec3 Flat(const glm::vec3& vector) {
    return {vector.x, 0.0f, vector.z};
}

/// Plays a clip on a model, if the model and the clip are there.
void Play(ecs::World& world, ecs::Entity model, std::string_view clip, animation::PlayOptions options) {
    animation::Animator* animator = world.TryGet<animation::Animator>(model);
    if (animator == nullptr) {
        return;
    }
    if (const std::optional<std::size_t> found = animator->Clips().Find(clip)) {
        animator->Play(clip, options);
    }
}

/// The length of a clip of a model, or 0 without it.
float ClipLength(ecs::World& world, ecs::Entity model, std::string_view clip) {
    const animation::Animator* animator = world.TryGet<animation::Animator>(model);
    if (animator == nullptr) {
        return 0.0f;
    }
    const std::optional<std::size_t> found = animator->Clips().Find(clip);
    return found.has_value() ? animator->Clips().clips[*found].duration : 0.0f;
}

/// "x:y:z|x:y:z", in legacy units from where the zombie stands.
std::vector<glm::vec3> ParsePatrol(std::string_view text, const glm::vec3& origin) {
    std::vector<glm::vec3> points;
    while (!text.empty()) {
        const std::size_t bar = text.find('|');
        const std::string_view point = text.substr(0, bar);
        glm::vec3 legacy{0.0f};
        const char* cursor = point.data();
        const char* const end = cursor + point.size();
        for (int axis = 0; axis < 3 && cursor < end; ++axis) {
            cursor = std::from_chars(cursor, end, legacy[axis]).ptr + 1; // Past the colon.
        }
        // Legacy space is left-handed: Z is mirrored.
        points.push_back(origin + glm::vec3(legacy.x, legacy.y, -legacy.z) * kLegacyUnit);
        text = bar == std::string_view::npos ? std::string_view{} : text.substr(bar + 1);
    }
    return points;
}

void HurtPlayer(scene::Scene& scene, ecs::Entity playerEntity, int damage) {
    ecs::World& world = scene.World();
    Player& player = world.Get<Player>(playerEntity);
    if (player.dead) {
        return;
    }
    const Arsenal* arsenal = world.TryGet<Arsenal>(playerEntity);
    // Armor takes half of each wound, for as long as it lasts.
    const int absorbed = std::min(player.armor, damage / 2);
    player.armor -= absorbed;
    player.health -= damage - absorbed;
    player.hurtTime = kHurtFlash;
    if (player.health > 0) {
        if (arsenal != nullptr) {
            scene.Sounds().Play(arsenal->hurt);
        }
        return;
    }
    player.health = 0;
    player.dead = true;
    player.reloading = false;
    player.message = "You are dead.";
    player.messageTime = kDeadMessageTime;
    if (arsenal != nullptr) {
        scene.Sounds().Play(arsenal->die);
    }
}

/// Wakes a zombie up and sends it to where the player is.
void Alert(Zombie& zombie, const glm::vec3& playerFeet) {
    if (zombie.state == ZombieState::Dying || zombie.state == ZombieState::Attacking) {
        return;
    }
    zombie.state = ZombieState::Chasing;
    zombie.lastSeen = playerFeet;
    zombie.timer = kZombieSearch;
}

void Kill(scene::Scene& scene, Zombie& zombie, Arsenal* arsenal) {
    zombie.state = ZombieState::Dying;
    zombie.health = 0;
    // The body stays where it fell, and no longer blocks the way or the shots.
    scene.Physics().RemoveCharacter(zombie.character);
    zombie.character = {};
    zombie.body = {};
    zombie.previousFeet = zombie.feet;
    Play(scene.World(), zombie.model, "die", {.loop = false, .fade = 0.1f, .restart = true});
    if (arsenal != nullptr) {
        scene.Sounds().Play(arsenal->bodyFall, {.position = zombie.feet});
    }
}

// Where the muzzle is, from the eye: right, low and ahead, where the weapons are held.
constexpr glm::vec3 kMuzzleOffset{0.08f, -0.075f, -0.65f};

// How long a shot lights the room, in seconds.
constexpr float kMuzzleFlashTime = 0.06f;

void Shoot(scene::Scene& scene, ecs::Entity playerEntity, Player& player, Arsenal* arsenal) {
    ecs::World& world = scene.World();
    physics::PhysicsWorld& physics = scene.Physics();
    const WeaponSpec& spec = kWeapons[static_cast<std::size_t>(player.weapon)];
    WeaponState& weapon = player.weapons[static_cast<std::size_t>(player.weapon)];
    --weapon.magazine;
    player.weaponBusy = spec.fireDelay;
    player.muzzleFlashTime = kMuzzleFlashTime;

    std::minstd_rand fallback;
    std::minstd_rand& random = arsenal != nullptr ? arsenal->random : fallback;
    if (arsenal != nullptr) {
        scene.Sounds().Play(arsenal->fire[static_cast<std::size_t>(player.weapon)]);
        const ecs::Entity model = arsenal->models[static_cast<std::size_t>(player.weapon)];
        // The clip must end before the next shot.
        const float speed = std::max(1.0f, ClipLength(world, model, "fire") / spec.fireDelay);
        Play(world, model, "fire", {.loop = false, .speed = speed, .fade = 0.0f, .restart = true});
    }

    const ecs::LocalTransform& eye = world.Get<ecs::LocalTransform>(playerEntity);
    const glm::vec3 aim = eye.rotation * math::kForward;
    const physics::BodyId self = physics.CharacterBody(player.character);
    std::vector<physics::RayHit> hits;
    for (int pellet = 0; pellet < spec.pellets; ++pellet) {
        const glm::vec3 direction = glm::normalize(aim + glm::vec3(Random(random, -spec.spread, spec.spread),
                                                                   Random(random, -spec.spread, spec.spread),
                                                                   Random(random, -spec.spread, spec.spread)));
        if (const std::optional<physics::RayHit> hit =
                physics.CastRay(eye.translation, direction, kWeaponRange, self)) {
            hits.push_back(*hit);
        }
    }

    // A flash and a puff of smoke at the muzzle, and where each pellet lands, blood on a zombie and
    // sparks anywhere else.
    if (const ParticleEffects* effects = world.TryGet<ParticleEffects>(playerEntity)) {
        const glm::vec3 muzzle = eye.translation + eye.rotation * kMuzzleOffset;
        scene.SpawnParticles(effects->muzzleFlash, muzzle, aim, 3);
        scene.SpawnParticles(effects->gunSmoke, muzzle, math::kUp, 6);
        std::vector<physics::BodyId> zombieBodies;
        world.Each<Zombie>([&](ecs::Entity, const Zombie& zombie) { zombieBodies.push_back(zombie.body); });
        for (const physics::RayHit& hit : hits) {
            const bool flesh = std::ranges::find(zombieBodies, hit.body) != zombieBodies.end();
            scene.SpawnParticles(flesh ? effects->blood : effects->sparks, hit.position, hit.normal, flesh ? 20 : 12);
        }
    }

    world.Each<Zombie>([&](ecs::Entity /*entity*/, Zombie& zombie) {
        if (zombie.state == ZombieState::Dying) {
            return;
        }
        int damage = 0;
        for (const physics::RayHit& hit : hits) {
            damage += hit.body == zombie.body ? spec.damage : 0;
        }
        if (damage > 0) {
            zombie.health -= damage;
            if (zombie.health <= 0) {
                Kill(scene, zombie, arsenal);
                ++player.kills;
                return;
            }
        }
        // A wound, or a shot close enough to be heard, brings it.
        if (damage > 0 || glm::distance(zombie.feet, player.feet) < kGunshotAlert) {
            Alert(zombie, player.feet);
        }
    });
}

void StartReload(scene::Scene& scene, Player& player, const Arsenal* arsenal) {
    const WeaponSpec& spec = kWeapons[static_cast<std::size_t>(player.weapon)];
    player.reloading = true;
    player.weaponBusy = spec.reloadTime;
    if (arsenal != nullptr) {
        scene.Sounds().Play(arsenal->reload[static_cast<std::size_t>(player.weapon)]);
        const ecs::Entity model = arsenal->models[static_cast<std::size_t>(player.weapon)];
        const float length = ClipLength(scene.World(), model, "reload");
        const float speed = length > 0.0f ? length / spec.reloadTime : 1.0f;
        Play(scene.World(), model, "reload", {.loop = false, .speed = speed, .fade = 0.1f, .restart = true});
    }
}

void PlaceWeapons(ecs::World& world, const Player& player, const Arsenal& arsenal) {
    for (std::size_t kind = 0; kind < kWeaponKinds; ++kind) {
        auto* transform = world.TryGet<ecs::LocalTransform>(arsenal.models[kind]);
        if (transform == nullptr) {
            continue;
        }
        float drop = kWeaponDrop;
        if (kind == static_cast<std::size_t>(player.weapon) && !player.dead) {
            drop = kWeaponDrop * (1.0f - std::clamp(player.switching / kSwitchTime, 0.0f, 1.0f));
        }
        transform->translation = kWeapons[kind].offset - math::kUp * drop;
        transform->scale = glm::vec3(kWeapons[kind].scale);
    }
}

/// Whether the zombie sees the player's eyes: close enough, in front of it unless it already hunts,
/// and with nothing in between.
bool Sees(const physics::PhysicsWorld& physics, const Zombie& zombie, const glm::vec3& playerEye,
          physics::BodyId playerBody) {
    const glm::vec3 eye = zombie.feet + math::kUp * kZombieEye;
    const glm::vec3 toPlayer = playerEye - eye;
    const float distance = glm::length(toPlayer);
    if (distance > kZombieSight || distance < 0.01f) {
        return false;
    }
    const bool hunting = zombie.state == ZombieState::Chasing || zombie.state == ZombieState::Attacking;
    if (!hunting) {
        const glm::vec3 facing(-std::sin(zombie.yaw), 0.0f, -std::cos(zombie.yaw));
        const glm::vec3 flat = Flat(toPlayer);
        if (glm::length(flat) > 0.01f && glm::dot(facing, glm::normalize(flat)) < std::cos(kZombieViewAngle)) {
            return false;
        }
    }
    const std::optional<physics::RayHit> hit = physics.CastRay(eye, toPlayer / distance, distance + 0.5f, zombie.body);
    return hit.has_value() && hit->body == playerBody;
}

void Groan(scene::Scene& scene, Zombie& zombie, Arsenal* arsenal, float step) {
    zombie.groanTime -= step;
    if (zombie.groanTime > 0.0f || arsenal == nullptr) {
        return;
    }
    zombie.groanTime = Random(arsenal->random, 4.0f, 9.0f);
    const std::size_t which = arsenal->random() % arsenal->groans.size();
    scene.Sounds().Play(arsenal->groans[which], {.position = zombie.feet, .minDistance = 2.0f, .maxDistance = 25.0f});
}

void Think(scene::Scene& scene, ecs::Entity playerEntity, Zombie& zombie, float step) {
    ecs::World& world = scene.World();
    physics::PhysicsWorld& physics = scene.Physics();
    Player& player = world.Get<Player>(playerEntity);
    Arsenal* arsenal = world.TryGet<Arsenal>(playerEntity);

    const glm::vec3 playerEye = player.feet + math::kUp * player.eyeHeight;
    const bool sees = !player.dead && Sees(physics, zombie, playerEye, physics.CharacterBody(player.character));
    const float distance = glm::length(Flat(player.feet - zombie.feet));
    std::optional<glm::vec3> target;
    bool facePlayer = false;

    switch (zombie.state) {
    case ZombieState::Waiting:
    case ZombieState::Patrolling:
        if (sees) {
            zombie.state = ZombieState::Chasing;
            zombie.lastSeen = player.feet;
            zombie.timer = kZombieSearch;
            if (arsenal != nullptr) {
                scene.Sounds().Play(arsenal->moan,
                                    {.position = zombie.feet, .minDistance = 2.0f, .maxDistance = 25.0f});
            }
            break;
        }
        if (zombie.state == ZombieState::Waiting) {
            zombie.timer -= step;
            if (zombie.timer <= 0.0f && !zombie.patrol.empty()) {
                zombie.state = ZombieState::Patrolling;
            }
        } else if (glm::length(Flat(zombie.patrol[zombie.nextPoint] - zombie.feet)) < 0.4f) {
            zombie.nextPoint = (zombie.nextPoint + 1) % zombie.patrol.size();
            zombie.state = ZombieState::Waiting;
            zombie.timer = kZombieWait;
        } else {
            target = zombie.patrol[zombie.nextPoint];
        }
        break;
    case ZombieState::Chasing:
        if (sees) {
            zombie.lastSeen = player.feet;
            zombie.timer = kZombieSearch;
        } else {
            zombie.timer -= step;
        }
        if (zombie.timer <= 0.0f || player.dead) {
            zombie.state = ZombieState::Waiting;
            zombie.timer = kZombieWait;
        } else if (sees && distance < kZombieReach) {
            zombie.state = ZombieState::Attacking;
            zombie.swing = 0.0f;
            zombie.struck = false;
            const float speed = std::max(ClipLength(world, zombie.model, "attack") / kZombieSwing, 0.01f);
            Play(world, zombie.model, "attack", {.loop = false, .speed = speed, .fade = 0.1f, .restart = true});
        } else if (glm::length(Flat(zombie.lastSeen - zombie.feet)) > 0.5f) {
            target = zombie.lastSeen;
        } else {
            facePlayer = sees;
        }
        Groan(scene, zombie, arsenal, step);
        break;
    case ZombieState::Attacking:
        facePlayer = true;
        zombie.swing += step;
        if (!zombie.struck && zombie.swing >= kZombieSwing * kZombieStrikeAt) {
            zombie.struck = true;
            if (distance < kZombieReach + kZombieReachMargin) {
                HurtPlayer(scene, playerEntity, kZombieDamage);
            }
        }
        if (zombie.swing >= kZombieSwing) {
            zombie.state = ZombieState::Chasing;
            zombie.timer = kZombieSearch;
            zombie.lastSeen = player.feet;
        }
        break;
    case ZombieState::Dying:
        return;
    }

    // It turns first, and walks once it roughly faces where it goes.
    float turn = 0.0f;
    if (target.has_value() || facePlayer) {
        const glm::vec3 toward = Flat((target.has_value() ? *target : player.feet) - zombie.feet);
        if (glm::length(toward) > 0.01f) {
            turn = std::remainder(YawOf(toward) - zombie.yaw, glm::two_pi<float>());
            zombie.yaw += std::clamp(turn, -kZombieTurnSpeed * step, kZombieTurnSpeed * step);
        }
    }
    const bool walking = target.has_value() && std::abs(turn) < glm::radians(45.0f);
    glm::vec3 velocity{0.0f};
    if (walking) {
        velocity = glm::vec3(-std::sin(zombie.yaw), 0.0f, -std::cos(zombie.yaw)) * kZombieSpeed;
    }
    if (physics.IsOnGround(zombie.character) && zombie.verticalSpeed <= 0.0f) {
        zombie.verticalSpeed = 0.0f;
    } else {
        zombie.verticalSpeed =
            std::min(zombie.verticalSpeed, physics.CharacterVelocity(zombie.character).y) - kGravity * step;
    }
    velocity.y = zombie.verticalSpeed;
    physics.MoveCharacter(zombie.character, velocity, step);
    zombie.previousFeet = zombie.feet;
    zombie.feet = physics.CharacterPosition(zombie.character);

    const float moved = glm::length(Flat(zombie.feet - zombie.previousFeet)) / step;
    zombie.stuck = (walking && moved < kStuckSpeed * kZombieSpeed) ? zombie.stuck + step : 0.0f;
    if (zombie.stuck >= kStuckTime) {
        zombie.stuck = 0.0f;
        if (zombie.state == ZombieState::Patrolling) {
            zombie.nextPoint = (zombie.nextPoint + 1) % zombie.patrol.size();
            zombie.state = ZombieState::Waiting;
            zombie.timer = kZombieWait;
        } else if (zombie.state == ZombieState::Chasing) {
            // Where it saw the player last is out of reach: it waits there for the player to show.
            zombie.lastSeen = zombie.feet;
        }
    }

    if (zombie.state != ZombieState::Attacking) {
        if (walking) {
            Play(world, zombie.model, "walk", {.speed = kZombieSpeed / kLegacyZombieSpeed, .fade = kZombieFade});
        } else {
            Play(world, zombie.model, "wait", {.fade = kZombieFade});
        }
    }
}

} // namespace

void SetUpCombat(scene::Scene& scene, ecs::Entity playerEntity, const std::string& directory) {
    ecs::World& world = scene.World();
    Arsenal arsenal;
    for (std::size_t kind = 0; kind < kWeaponKinds; ++kind) {
        arsenal.fire[kind] = LoadSound(scene, kWeapons[kind].fireSound);
        arsenal.reload[kind] = LoadSound(scene, kWeapons[kind].reloadSound);
    }
    arsenal.change = LoadSound(scene, "change_weapon.wav");
    arsenal.hurt = LoadSound(scene, "hurt.wav");
    arsenal.die = LoadSound(scene, "die.wav");
    arsenal.bodyFall = LoadSound(scene, "body_fall.wav");
    arsenal.moan = LoadSound(scene, "zombie_moan.wav");
    arsenal.groans = {LoadSound(scene, "zombie_groan1.wav"), LoadSound(scene, "zombie_groan2.wav"),
                      LoadSound(scene, "zombie_groan3.wav")};

    for (std::size_t kind = 0; kind < kWeaponKinds; ++kind) {
        const std::string file = kWeapons[kind].model;
        const std::string path = directory.empty() ? file : directory + "/" + file;
        core::Result<const scene::Model*> model = scene.Assets().LoadModel(path);
        if (!model) {
            DAI_LOG_WARNING("sandbox", "No model for a weapon: {}", core::Format(model.error()));
            continue;
        }
        core::Result<ecs::Entity> instance = scene::Instantiate(world, **model);
        if (!instance || !world.SetParent(*instance, playerEntity)) {
            DAI_LOG_WARNING("sandbox", "Cannot hold the weapon '{}'", path);
            continue;
        }
        arsenal.models[kind] = *instance;
        // Held right in front of the flashlight, a weapon would cast a shadow over half the room.
        world.Each<scene::MeshRenderer>([&world, root = *instance](ecs::Entity entity, scene::MeshRenderer& renderer) {
            renderer.castsShadows = renderer.castsShadows && !world.IsDescendantOf(entity, root);
        });
        world.Each<scene::SkinnedMeshRenderer>(
            [&world, root = *instance](ecs::Entity entity, scene::SkinnedMeshRenderer& renderer) {
                renderer.castsShadows = renderer.castsShadows && !world.IsDescendantOf(entity, root);
            });
    }
    const Arsenal& added = world.Add<Arsenal>(playerEntity, std::move(arsenal));
    PlaceWeapons(world, world.Get<Player>(playerEntity), added);
}

void AddZombie(scene::Scene& scene, ecs::Entity node, ecs::Entity model, std::string_view patrol) {
    ecs::World& world = scene.World();
    physics::PhysicsWorld& physics = scene.Physics();
    ecs::LocalTransform& transform = world.Get<ecs::LocalTransform>(node);
    // The legacy editor left zombies above the floor, where gravity used to bring them down.
    const glm::vec3 above = transform.translation + math::kUp * kFloorSearch;
    if (const std::optional<physics::RayHit> floor = physics.CastRay(above, -math::kUp, kFloorSearch + kFloorDrop)) {
        transform.translation = floor->position;
    }

    Zombie zombie;
    zombie.model = model;
    physics::CharacterConfig config;
    config.radius = kZombieRadius;
    config.height = kZombieHeight;
    zombie.character = physics.AddCharacter(config, transform.translation);
    zombie.body = physics.CharacterBody(zombie.character);
    zombie.feet = transform.translation;
    zombie.previousFeet = zombie.feet;
    zombie.yaw = YawOf(transform.rotation * math::kForward);
    zombie.patrol = ParsePatrol(patrol, zombie.feet);
    // Each one starts its patrol at its own time.
    std::minstd_rand random{static_cast<std::uint32_t>(node.Index()) + 1U};
    zombie.timer = Random(random, 0.0f, kZombieWait);
    zombie.groanTime = Random(random, 1.0f, 6.0f);
    world.Add<Zombie>(node, std::move(zombie));
}

void GiveShotgun(Player& player) {
    WeaponState& shotgun = player.weapons[static_cast<std::size_t>(WeaponKind::Shotgun)];
    if (shotgun.owned) {
        AddAmmo(player, WeaponKind::Shotgun, kWeapons[static_cast<std::size_t>(WeaponKind::Shotgun)].magazine);
        return;
    }
    shotgun.owned = true;
    shotgun.magazine = kWeapons[static_cast<std::size_t>(WeaponKind::Shotgun)].magazine;
    if (!player.dead) {
        player.weapon = WeaponKind::Shotgun;
        player.reloading = false;
        player.weaponBusy = kSwitchTime;
        player.switching = 0.0f;
    }
}

void AddAmmo(Player& player, WeaponKind weapon, int rounds) {
    WeaponState& state = player.weapons[static_cast<std::size_t>(weapon)];
    state.reserve = std::min(state.reserve + rounds, kWeapons[static_cast<std::size_t>(weapon)].maxReserve);
}

void UpdateWeapons(scene::Scene& scene, ecs::Entity playerEntity, const input::InputState& input,
                   const input::ActionMap& actions, float delta) {
    ecs::World& world = scene.World();
    Player& player = world.Get<Player>(playerEntity);
    Arsenal* arsenal = world.TryGet<Arsenal>(playerEntity);
    player.weaponBusy = std::max(player.weaponBusy - delta, 0.0f);
    player.switching = std::min(player.switching + delta, kSwitchTime);
    player.hurtTime = std::max(player.hurtTime - delta, 0.0f);

    if (!player.dead) {
        std::optional<WeaponKind> wanted;
        if (actions.WasPressed(input, SelectHandgun)) {
            wanted = WeaponKind::Handgun;
        } else if (actions.WasPressed(input, SelectShotgun)) {
            wanted = WeaponKind::Shotgun;
        } else if (actions.WasPressed(input, NextWeapon) || input.WheelDelta().y != 0.0f) {
            wanted = player.weapon == WeaponKind::Handgun ? WeaponKind::Shotgun : WeaponKind::Handgun;
        }
        if (wanted.has_value() && *wanted != player.weapon && player.weapons[static_cast<std::size_t>(*wanted)].owned) {
            player.weapon = *wanted;
            player.reloading = false;
            player.weaponBusy = kSwitchTime;
            player.switching = 0.0f;
            if (arsenal != nullptr) {
                scene.Sounds().Play(arsenal->change);
            }
        }

        const WeaponSpec& spec = kWeapons[static_cast<std::size_t>(player.weapon)];
        WeaponState& weapon = player.weapons[static_cast<std::size_t>(player.weapon)];
        if (player.reloading && player.weaponBusy <= 0.0f) {
            const int moved = std::min(spec.magazine - weapon.magazine, weapon.reserve);
            weapon.magazine += moved;
            weapon.reserve -= moved;
            player.reloading = false;
        }
        const bool fire = actions.IsDown(input, Fire) || actions.Value(input, FireTrigger) > 0.5f;
        const bool reload = actions.WasPressed(input, Reload) || (fire && weapon.magazine == 0);
        if (player.weaponBusy <= 0.0f && !player.reloading) {
            if (fire && weapon.magazine > 0) {
                Shoot(scene, playerEntity, player, arsenal);
            } else if (reload && weapon.magazine < spec.magazine && weapon.reserve > 0) {
                StartReload(scene, player, arsenal);
            }
        }
    }
    if (arsenal != nullptr) {
        PlaceWeapons(world, player, *arsenal);
    }
}

void UpdateZombies(scene::Scene& scene, ecs::Entity playerEntity, float step) {
    std::vector<ecs::Entity> zombies;
    scene.World().Each<Zombie>([&zombies](ecs::Entity entity, const Zombie& /*zombie*/) { zombies.push_back(entity); });
    // Not inside Each: a zombie that hurts the player touches other components.
    for (const ecs::Entity entity : zombies) {
        Think(scene, playerEntity, scene.World().Get<Zombie>(entity), step);
    }
}

void PlaceZombies(scene::Scene& scene) {
    const float alpha = scene.FixedAlpha();
    scene.World().Each<Zombie, ecs::LocalTransform>(
        [alpha](ecs::Entity /*entity*/, const Zombie& zombie, ecs::LocalTransform& transform) {
            transform.translation = glm::mix(zombie.previousFeet, zombie.feet, alpha);
            transform.rotation = glm::angleAxis(zombie.yaw, math::kUp);
        });
}

} // namespace sandbox
