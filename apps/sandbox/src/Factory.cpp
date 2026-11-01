// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "Factory.hpp"

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/renderer/Components.hpp>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr float kStandHeight = 1.85f;
constexpr float kCrouchHeight = 0.9f;
constexpr float kEyeHeight = 1.65f; // Above the floor.
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
constexpr float kMaxPitch = glm::radians(89.0f);
constexpr float kStickLookSpeed = 2.5f; // Radians per second.
constexpr float kMessageTime = 3.0f;    // Seconds.

// Darkness, until the lights of Phase 7: what is far away fades to black, and the flashlight pushes
// that distance back.
constexpr glm::vec3 kDarkness{0.005f, 0.006f, 0.008f};
constexpr float kDarkDensity = 0.22f;
constexpr float kFlashlightDensity = 0.07f;

enum class PickupKind { Health, Armor, HandgunAmmo, ShotgunAmmo, Flashlight, RedCard };

struct Pickup {
    PickupKind kind;
};

/// The box around a ladder, in which the player climbs.
struct Ladder {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
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
    return std::nullopt;
}

void Say(Player& player, std::string message) {
    player.message = std::move(message);
    player.messageTime = kMessageTime;
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
        player.handgunAmmo += kHandgunAmmoBonus;
        Say(player, "Handgun ammo");
        break;
    case PickupKind::ShotgunAmmo:
        player.shotgunAmmo += kShotgunAmmoBonus;
        Say(player, "Shotgun ammo");
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

    const float targetEye = player.crouching ? kCrouchEyeHeight : kEyeHeight;
    const float eyeStep = kEyeSpeed * delta;
    player.eyeHeight = std::clamp(targetEye, player.eyeHeight - eyeStep, player.eyeHeight + eyeStep);
    transform.translation =
        glm::mix(player.previousFeet, player.feet, scene.FixedAlpha()) + math::kUp * player.eyeHeight;

    player.jumpPressed = player.jumpPressed || actions.WasPressed(input, Jump);
    player.interactPressed = player.interactPressed || actions.WasPressed(input, Interact);
    if (player.hasFlashlight && actions.WasPressed(input, ToggleFlashlight)) {
        player.flashlightOn = !player.flashlightOn;
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

/// What the player does on every fixed step: walking, jumping, crouching and climbing.
void MovePlayer(ecs::Entity entity, scene::Scene& scene, const input::InputState& input,
                const input::ActionMap& actions, float step) {
    ecs::World& world = scene.World();
    physics::PhysicsWorld& physics = scene.Physics();
    Player& player = world.Get<Player>(entity);

    const bool wantsCrouch = actions.IsDown(input, Crouch);
    if (wantsCrouch != player.crouching &&
        physics.SetCharacterHeight(player.character, wantsCrouch ? kCrouchHeight : kStandHeight)) {
        player.crouching = wantsCrouch;
    }

    // Only the direction the player faces counts: looking up does not slow them down.
    const glm::quat heading = math::FromYawPitchRoll(player.yaw, 0.0f, 0.0f);
    const float forward = actions.Value(input, MoveForward);
    glm::vec3 wish = heading * glm::vec3(actions.Value(input, MoveRight), 0.0f, -forward);
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
    if (const std::optional<float> top = LadderTop(world, player.feet); top.has_value() && !jump) {
        player.verticalSpeed = forward * kClimbSpeed * (player.pitch < kLookDownOnLadder ? -1.0f : 1.0f);
        // The player waits just above the top, from where they can step off to any side.
        if (player.feet.y >= *top + kLadderTopMargin) {
            player.verticalSpeed = std::min(player.verticalSpeed, 0.0f);
        }
    } else if (onGround) {
        player.verticalSpeed = jump && !player.crouching ? kJumpSpeed : 0.0f;
    } else {
        // A ceiling takes the speed away.
        player.verticalSpeed =
            std::min(player.verticalSpeed, physics.CharacterVelocity(player.character).y) - kGravity * step;
    }
    velocity.y = player.verticalSpeed;

    physics.MoveCharacter(player.character, velocity, step);
    player.previousFeet = player.feet;
    player.feet = physics.CharacterPosition(player.character);
}

void UpdatePickups(ecs::World& world, ecs::Entity playerEntity, float delta) {
    Player& player = world.Get<Player>(playerEntity);
    const glm::vec3 eye = world.Get<ecs::LocalTransform>(playerEntity).translation;
    const glm::quat spin = glm::angleAxis(kPickupSpin * delta, math::kUp);

    std::vector<ecs::Entity> taken;
    world.Each<Pickup, ecs::LocalTransform>(
        [&](ecs::Entity entity, const Pickup& pickup, ecs::LocalTransform& transform) {
            transform.rotation = spin * transform.rotation;
            if (glm::distance(transform.translation, eye) < kPickupRadius + kEyeHeight * 0.5f) {
                Collect(player, pickup.kind);
                taken.push_back(entity);
            }
        });
    for (const ecs::Entity entity : taken) {
        world.Destroy(entity);
    }
}

void UpdateDoors(ecs::World& world, ecs::Entity playerEntity, float delta) {
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
            }
        }
        if (door.open && door.closable) {
            door.timeLeft -= delta;
            // Never down on the player.
            if (door.timeLeft <= 0.0f && distance >= kDoorRadius) {
                door.open = false;
            }
        }
        const float step = kDoorSpeed * delta;
        door.rise = std::clamp(door.rise + (door.open ? step : -step), 0.0f, kDoorRise);
        transform.translation = door.closedPosition + math::kUp * door.rise;
    });
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
    actions.Bind(Quit, Key::Escape);
    actions.Bind(Quit, GamepadButton::Back);
    return actions;
}

void Teleport(scene::Scene& scene, ecs::Entity entity, const glm::vec3& eye) {
    Player& player = scene.World().Get<Player>(entity);
    player.feet = eye - math::kUp * player.eyeHeight;
    player.previousFeet = player.feet;
    player.verticalSpeed = 0.0f;
    scene.Physics().SetCharacterPosition(player.character, player.feet);
}

core::Result<ecs::Entity> BuildLevel(scene::Scene& scene, const renderer::Model& model) {
    ecs::World& world = scene.World();
    core::Result<ecs::Entity> root = renderer::Instantiate(world, model);
    if (!root) {
        return root;
    }
    physics::PhysicsWorld& physics = scene.Physics();

    // What the game needs from the geometry: the level to walk on, and the size of the doors.
    std::map<std::string_view, const renderer::Model::Node*> modelNodes;
    for (const renderer::Model::Node& node : model.nodes) {
        modelNodes.emplace(node.name, &node);
        if (node.name != "level") {
            continue;
        }
        for (const renderer::Model::Part& part : node.parts) {
            if (core::Result<physics::BodyId> body = physics.AddStaticMesh(part.positions, part.indices, node.local);
                !body) {
                return tl::unexpected(std::move(body.error()));
            }
        }
    }

    ecs::Entity playerEntity = world.Create();
    glm::vec3 eye{0.0f, kEyeHeight, 3.0f};
    world.Add<Player>(playerEntity);
    world.Add<renderer::Camera>(playerEntity);
    physics::CharacterConfig body;
    body.height = kStandHeight;
    world.Get<Player>(playerEntity).character = physics.AddCharacter(body, eye - math::kUp * kEyeHeight);

    std::vector<int> lockedDoors;
    std::vector<std::pair<ecs::Entity, std::string>> nodes;
    world.Each<renderer::NodeName>(
        [&nodes](ecs::Entity entity, const renderer::NodeName& name) { nodes.emplace_back(entity, name.value); });
    for (const auto& [entity, name] : nodes) {
        const NodeDescription node = Parse(name);
        if (node.kind == "card_reader") {
            lockedDoors.push_back(Number(node, "id", 0));
        }
    }
    for (const auto& [entity, name] : nodes) {
        const NodeDescription node = Parse(name);
        const glm::vec3 position = world.Get<ecs::LocalTransform>(entity).translation;
        if (node.kind == "player") {
            eye = position;
            if (const std::optional<physics::RayHit> floor = physics.CastRay(position, -math::kUp, 20.0f)) {
                eye = floor->position + math::kUp * kEyeHeight;
            }
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
                for (const renderer::Model::Part& part : found->second->parts) {
                    for (const glm::vec3& vertex : part.positions) {
                        low = glm::min(low, vertex);
                        high = glm::max(high, vertex);
                    }
                }
                if (low.x <= high.x) {
                    math::Transform transform;
                    transform.translation = position;
                    const glm::vec3 halfExtents = glm::max((high - low) * 0.5f, glm::vec3(0.05f));
                    core::Result<physics::BodyId> box =
                        physics.AddKinematicBox(halfExtents, (low + high) * 0.5f, transform);
                    if (!box) {
                        return tl::unexpected(std::move(box.error()));
                    }
                    world.Add<physics::KinematicBody>(entity, *box);
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
    ecs::Schedule& fixed = scene.FixedSystems();
    fixed.Add("Movement", [&, player](ecs::World& world, core::Seconds step) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            MovePlayer(entity, scene, input, actions, static_cast<float>(step.count()));
        }
    });
    fixed.Add("Doors", [player](ecs::World& world, core::Seconds step) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdateDoors(world, entity, static_cast<float>(step.count()));
        }
    });

    ecs::Schedule& systems = scene.FrameSystems();
    systems.Add("Look", [&, player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            LookAround(entity, scene, input, actions, settings.mouseSensitivity, static_cast<float>(delta.count()));
        }
    });
    systems.Add("Pickups", [player](ecs::World& world, core::Seconds delta) {
        if (const ecs::Entity entity = player(); world.IsAlive(entity)) {
            UpdatePickups(world, entity, static_cast<float>(delta.count()));
        }
    });
    systems.Add("Darkness", [&, player](ecs::World& world, core::Seconds /*delta*/) {
        if (const Player* state = world.TryGet<Player>(player())) {
            const float density = state->flashlightOn ? kFlashlightDensity : kDarkDensity;
            scene.SetAtmosphere({kDarkness, density * settings.darkness});
        }
    });
}

} // namespace sandbox
