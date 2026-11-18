// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/Entity.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Level files (ADR-0054): what a level places, as data that the game loads and the editor edits.
namespace daidalos::ecs {
class World;
}

namespace daidalos::scene {

class Scene;
struct Model;

/// ECS component: the level entity an entity was made from (LevelEntity::id).
struct LevelEntityId {
    std::uint64_t value = 0;
};

/// ECS component: the model an entity places, and the root entity its nodes hang from (a child of the entity).
struct ModelInstance {
    const Model* model = nullptr;
    ecs::Entity root;
};

/// The entity of a model's node of this name, under the model's root (ModelInstance::root), or null.
[[nodiscard]] ecs::Entity FindNodeEntity(const ecs::World& world, ecs::Entity root, std::string_view name);

/// A part of a model that a level entity places: the entity and the name of the node.
struct LevelNodeRef {
    std::uint64_t entity = 0;
    std::string node;
    [[nodiscard]] bool operator==(const LevelNodeRef&) const = default;
};

/// A particle emitter of a level (ADR-0053): its effect file, emitting as the file says unless a burst
/// replaces it, at the entity or on a model node's triangles.
struct LevelParticles {
    std::string effect;
    /// Emits only this burst, again and again, instead of the file's emission: impacts shown on their own.
    std::optional<ParticleBurst> burst;
    std::optional<LevelNodeRef> surface;
    bool enabled = true;
    [[nodiscard]] bool operator==(const LevelParticles&) const;
};

struct LevelAttractor {
    /// The only effect it moves, or empty for every effect.
    std::string effect;
    float strength = 1.0f;
    float vortex = 0.0f;
    float radius = 0.0f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const LevelAttractor&) const = default;
};

struct LevelWeather {
    std::string effect;
    float rate = 50.0f;
    float reach = 15.0f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const LevelWeather&) const = default;
};

struct LevelRibbon {
    std::string effect;
    /// The entity where a line or bolt ends, or 0.
    std::uint64_t target = 0;
    bool enabled = true;
    [[nodiscard]] bool operator==(const LevelRibbon&) const = default;
};

struct LevelLightning {
    /// The ribbon file of its bolts.
    std::string bolt;
    /// The entity whose light flashes, or 0.
    std::uint64_t light = 0;
    float flash = 30.0f;
    glm::vec2 interval{6.0f, 15.0f};
    float duration = 0.5f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const LevelLightning&) const = default;
};

/// One entity of a level file.
struct LevelEntity {
    /// Stable, positive, unique in the level.
    std::uint64_t id = 0;
    std::string name;
    /// The parent's id, or 0 at the root.
    std::uint64_t parent = 0;
    math::Transform transform;
    /// A glTF file, relative to the level's directory, or empty.
    std::string model;
    /// A prefab (ADR-0078): a level file, relative to the level's directory, whose entities are made under this
    /// one, as a model's nodes are; or empty. They are not entities of this level and have no ids in it.
    std::string prefab;
    std::optional<Light> light;
    std::optional<FogVolume> fog;
    std::optional<WindVolume> wind;
    std::optional<LevelParticles> particles;
    std::optional<LevelAttractor> attractor;
    std::optional<LevelWeather> weather;
    std::optional<LevelRibbon> ribbon;
    std::optional<LevelLightning> lightning;
    /// Which rays see its meshes and those under it (ADR-0059).
    std::optional<RayVisibility> visibility;
    /// A decal its box projects (ADR-0060).
    std::optional<Decal> decal;
    /// Wet air in its box (ADR-0061).
    std::optional<WetVolume> wet;
    /// A room or a doorway for portal visibility, its box (ADR-0089).
    std::optional<VisibilityCell> cell;
    /// A box that plays a sequence when the player enters (ADR-0071); its file is relative to the level.
    std::optional<Trigger> trigger;
    /// A loose object (ADR-0073): physics moves it.
    std::optional<Body> body;
    std::optional<Tags> game;
    [[nodiscard]] bool operator==(const LevelEntity&) const;
};

/// The sky, fog, wind and exposure of a level.
struct LevelEnvironment {
    /// The six faces of the sky, as a path without its suffix: "Sky/sky1" reads Sky/sky1_right.png, _left, _up,
    /// _down, _front and _back. Empty for none.
    std::string skyFaces;
    float skyIntensity = 1.0f;
    /// Degrees around the vertical axis.
    float skyYaw = 0.0f;
    float fogDensity = 0.0f;
    glm::vec3 fogAlbedo{0.9f};
    float fogAnisotropy = 0.3f;
    float ev100 = 0.0f;
    bool autoExposure = false;
    /// Stops below and above ev100 that the eye adapts to (ADR-0048).
    float exposureMin = 0.0f;
    float exposureMax = 0.0f;
    glm::vec3 wind{0.0f};
    float windGust = 0.0f;
    float windGustFrequency = 0.2f;
    [[nodiscard]] bool operator==(const LevelEnvironment&) const = default;
};

struct Level {
    LevelEnvironment environment;
    /// Parents may come after their children: LevelInstance orders them.
    std::vector<LevelEntity> entities;

    [[nodiscard]] const LevelEntity* Find(std::uint64_t id) const;
    [[nodiscard]] LevelEntity* Find(std::uint64_t id);
    /// One more than the highest id.
    [[nodiscard]] std::uint64_t NextId() const;
    [[nodiscard]] bool operator==(const Level&) const = default;
};

inline constexpr std::uint32_t kLevelVersion = 1;

/// Reads a level file. Unknown keys, wrong types, ids that are not unique or not positive, and parents that
/// are missing or loop are errors that say where.
[[nodiscard]] core::Result<Level> ParseLevel(std::string_view json);
/// Writes a level file: keys sorted, two-space indents, the shortest numbers that read back the same, and
/// defaults left out.
[[nodiscard]] std::string WriteLevel(const Level& level);

/// Reads and parses a level file.
[[nodiscard]] core::Result<Level> LoadLevelFile(const std::filesystem::path& file);
/// Writes it, through a temporary file that replaces the old one, so a failed save loses nothing.
[[nodiscard]] core::Result<void> SaveLevelFile(const std::filesystem::path& file, const Level& level);

/// A level of one entity that places a glTF model: how a bare glTF file loads.
[[nodiscard]] Level LevelOfModel(std::string_view modelPath);

/// The models a level places, relative to the asset root, each once: what a loading screen loads first.
/// levelPath is the level file's path under the asset root.
[[nodiscard]] std::vector<std::string> LevelModels(const Level& level, std::string_view levelPath);

/// Rewrites the paths a level holds (models, effects, the sky) so that they name the same files once the level
/// is saved under toLevelPath instead of fromLevelPath, both under the asset root.
void Relocate(Level& level, std::string_view fromLevelPath, std::string_view toLevelPath);

/// Whether a file is a prefab, by its name: "<name>.prefab.json", a level file whose entities other levels place.
[[nodiscard]] bool IsPrefabPath(std::string_view path);

/// The deepest that prefabs place prefabs, so that one placing itself, directly or not, ends.
inline constexpr std::size_t kMaxPrefabDepth = 8;

/// What LevelInstance::Sync did.
struct LevelSync {
    /// Entities made, new or again, and destroyed.
    std::size_t created = 0;
    std::size_t destroyed = 0;
    /// Entities whose transform alone changed, moved in place.
    std::size_t moved = 0;
    /// Whether any entity made or destroyed places a model or a prefab: what an editor's picking must learn again.
    bool models = false;
};

/// The entities of a level in a scene. Models and effects come from the scene's assets, by paths relative to
/// the level's directory; a model loaded already (LevelModels) is not read again. What fails to load is
/// logged and left out, so that one missing effect does not lose the level.
class LevelInstance {
public:
    /// levelPath is the level file's path under the asset root.
    LevelInstance(Scene& scene, std::string_view levelPath);

    /// Sets the environment and creates every entity, parents first.
    void Build(const Level& level);
    /// Creates one entity, or replaces the one of the same id; its children stay, under the new one. Its
    /// parent must exist first.
    void Create(const Level& level, const LevelEntity& entity);
    /// Destroys an entity and its descendants, and the emitters they placed on other entities' surfaces.
    void Destroy(std::uint64_t id);
    /// Brings the scene from one state of the level to the next: destroys the entities that went, moves those
    /// whose transform alone changed, makes again those that changed otherwise (and the emitters on their
    /// surfaces), creates the new ones, and applies the environment if it changed. Undo, redo and every edit of
    /// the level editor go through it.
    LevelSync Sync(const Level& from, const Level& to);
    /// Applies the environment to the scene: sky, fog, wind and exposure.
    void ApplyEnvironment(const LevelEnvironment& environment);
    /// Makes again the entities that place prefabs, which then read their files again: after a prefab is saved.
    /// Returns how many.
    std::size_t RemakePrefabs(const Level& level);

    [[nodiscard]] ecs::Entity Find(std::uint64_t id) const;
    /// The level id of an entity or of the nearest ancestor made from one, or 0: what was clicked.
    [[nodiscard]] std::uint64_t IdOf(ecs::Entity entity) const;
    [[nodiscard]] const std::string& Directory() const { return m_directory; }
    /// A path relative to the level's directory, as a path under the asset root.
    [[nodiscard]] std::string Resolve(std::string_view relative) const;

private:
    /// The entities of a prefab, placed under an entity of the level that places it: they get no LevelEntityId,
    /// so that what is clicked in them is the entity placing them. chain holds the prefabs placing this one.
    LevelInstance(Scene& scene, std::string_view prefabPath, std::vector<std::string> chain);
    /// Makes the prefab an entity places, under it.
    void Place(const LevelEntity& entity, ecs::Entity holder);

    /// Creates or replaces one entity, without linking the ribbons and lightning to their targets.
    void Make(const LevelEntity& entity);
    void Link(const Level& level);
    /// The entities of a level in an order that creates parents, and the models emitters sit on, first.
    [[nodiscard]] static std::vector<const LevelEntity*> CreationOrder(const Level& level);

    Scene* m_scene;
    std::string m_directory;
    std::unordered_map<std::uint64_t, ecs::Entity> m_entities;
    /// The entity an emitter on another model's surface was put on, by the id of the level entity it belongs to.
    std::unordered_map<std::uint64_t, ecs::Entity> m_surfaceEmitters;
    /// The prefabs placed, by the id of the entity placing each.
    std::unordered_map<std::uint64_t, std::unique_ptr<LevelInstance>> m_prefabs;
    /// Set in a prefab's instance: its entities are parts of the entity that places it.
    bool m_part = false;
    /// The prefab files placing this instance, outermost first, itself last.
    std::vector<std::string> m_chain;
};

} // namespace daidalos::scene
