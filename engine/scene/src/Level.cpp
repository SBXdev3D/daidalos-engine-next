// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";

ecs::Entity FindNodeEntityIn(const ecs::World& world, ecs::Entity root, std::string_view name) {
    for (ecs::Entity child = world.FirstChild(root); !child.IsNull(); child = world.NextSibling(child)) {
        if (const NodeName* named = world.TryGet<NodeName>(child); named != nullptr && named->value == name) {
            return child;
        }
        if (const ecs::Entity found = FindNodeEntityIn(world, child, name); !found.IsNull()) {
            return found;
        }
    }
    return {};
}

/// The triangles of a model's node, all its parts together, in the node's frame.
std::shared_ptr<const MeshSurface> SurfaceOf(const Model& model, std::string_view name) {
    const auto node = std::ranges::find(model.nodes, name, &Model::Node::name);
    if (node == model.nodes.end()) {
        return nullptr;
    }
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    for (const std::uint32_t index : node->parts) {
        const Model::Part& part = model.parts[index];
        const auto first = static_cast<std::uint32_t>(positions.size());
        positions.insert(positions.end(), part.positions.begin(), part.positions.end());
        for (const std::uint32_t vertex : part.indices) {
            indices.push_back(first + vertex);
        }
    }
    return MakeMeshSurface(positions, indices);
}

} // namespace

ecs::Entity FindNodeEntity(const ecs::World& world, ecs::Entity root, std::string_view name) {
    return FindNodeEntityIn(world, root, name);
}

LevelInstance::LevelInstance(Scene& scene, std::string_view levelPath)
    : m_scene(&scene), m_directory(std::filesystem::path(levelPath).parent_path().generic_string()) {}

LevelInstance::LevelInstance(Scene& scene, std::string_view prefabPath, std::vector<std::string> chain)
    : LevelInstance(scene, prefabPath) {
    m_part = true;
    m_chain = std::move(chain);
}

void LevelInstance::Place(const LevelEntity& entity, ecs::Entity holder) {
    const std::string path = std::filesystem::path(Resolve(entity.prefab)).lexically_normal().generic_string();
    if (std::ranges::find(m_chain, path) != m_chain.end() || m_chain.size() >= kMaxPrefabDepth) {
        DAI_LOG_WARNING(kModule, "Level entity {} '{}': the prefab '{}' places itself", entity.id, entity.name, path);
        return;
    }
    core::Result<std::shared_ptr<const Level>> prefab = m_scene->Assets().LoadPrefab(path);
    if (!prefab) {
        DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(prefab.error()));
        return;
    }
    std::vector<std::string> chain = m_chain;
    chain.push_back(path);
    std::unique_ptr<LevelInstance> placed(new LevelInstance(*m_scene, path, std::move(chain)));
    ecs::World& world = m_scene->World();
    for (const LevelEntity* part : CreationOrder(**prefab)) {
        placed->Make(*part);
        if (part->parent == 0) {
            [[maybe_unused]] const core::Result<void> attached = world.SetParent(placed->Find(part->id), holder);
        }
    }
    placed->Link(**prefab);
    m_prefabs[entity.id] = std::move(placed);
}

std::size_t LevelInstance::RemakePrefabs(const Level& level) {
    std::size_t count = 0;
    for (const LevelEntity* entity : CreationOrder(level)) {
        if (!entity->prefab.empty()) {
            Make(*entity);
            ++count;
        }
    }
    Link(level);
    return count;
}

std::string LevelInstance::Resolve(std::string_view relative) const {
    return m_directory.empty() ? std::string(relative) : m_directory + "/" + std::string(relative);
}

void LevelInstance::ApplyEnvironment(const LevelEnvironment& environment) {
    renderer::Atmosphere atmosphere = m_scene->Atmosphere();
    atmosphere.sky = {};
    if (!environment.skyFaces.empty()) {
        // Named as the legacy engine named them, in the order of a cube's faces (ADR-0036).
        const std::string prefix = Resolve(environment.skyFaces);
        const std::array<std::string, 6> faces{prefix + "_right.png", prefix + "_left.png",  prefix + "_up.png",
                                               prefix + "_down.png",  prefix + "_front.png", prefix + "_back.png"};
        if (core::Result<rhi::TextureHandle> sky = m_scene->Assets().LoadCubeTexture(faces)) {
            atmosphere.sky = *sky;
        } else {
            DAI_LOG_WARNING(kModule, "The level's sky is missing: {}", core::Format(sky.error()));
        }
    }
    atmosphere.skyIntensity = environment.skyIntensity;
    atmosphere.skyYaw = glm::radians(environment.skyYaw);
    atmosphere.fog = {
        .density = environment.fogDensity, .albedo = environment.fogAlbedo, .anisotropy = environment.fogAnisotropy};
    atmosphere.ev100 = environment.ev100;
    atmosphere.autoExposure = environment.autoExposure;
    atmosphere.ev100Min = environment.ev100 - environment.exposureMin;
    atmosphere.ev100Max = environment.ev100 + environment.exposureMax;
    atmosphere.wind = environment.wind;
    atmosphere.windGust = environment.windGust;
    atmosphere.windGustFrequency = environment.windGustFrequency;
    m_scene->SetAtmosphere(atmosphere);
}

void LevelInstance::Build(const Level& level) {
    ApplyEnvironment(level.environment);
    for (const LevelEntity* entity : CreationOrder(level)) {
        Make(*entity);
    }
    Link(level);
}

std::vector<const LevelEntity*> LevelInstance::CreationOrder(const Level& level) {
    // Parents first, and the models that emitters sit on, whatever the file's order.
    std::set<std::uint64_t> ids;
    for (const LevelEntity& entity : level.entities) {
        ids.insert(entity.id);
    }
    const auto waitsFor = [&ids](const LevelEntity& entity, const std::set<std::uint64_t>& created) {
        if (entity.parent != 0 && !created.contains(entity.parent)) {
            return true;
        }
        if (entity.particles && entity.particles->surface) {
            const std::uint64_t owner = entity.particles->surface->entity;
            return owner != entity.id && ids.contains(owner) && !created.contains(owner);
        }
        return false;
    };
    std::vector<const LevelEntity*> order;
    order.reserve(level.entities.size());
    std::set<std::uint64_t> created;
    std::vector<const LevelEntity*> waiting;
    for (const LevelEntity& entity : level.entities) {
        waiting.push_back(&entity);
    }
    while (!waiting.empty()) {
        const std::size_t before = waiting.size();
        std::erase_if(waiting, [&](const LevelEntity* entity) {
            if (waitsFor(*entity, created)) {
                return false;
            }
            order.push_back(entity);
            created.insert(entity->id);
            return true;
        });
        if (waiting.size() == before) {
            // ParseLevel refuses loops and missing parents; a level built in code may still have them.
            DAI_LOG_WARNING(kModule, "{} level entities have no parent to hang from", waiting.size());
            break;
        }
    }
    return order;
}

LevelSync LevelInstance::Sync(const Level& from, const Level& to) {
    if (!(from.environment == to.environment)) {
        ApplyEnvironment(to.environment);
    }
    std::unordered_map<std::uint64_t, const LevelEntity*> previous;
    for (const LevelEntity& entity : from.entities) {
        previous.emplace(entity.id, &entity);
    }
    std::set<std::uint64_t> next;
    for (const LevelEntity& entity : to.entities) {
        next.insert(entity.id);
    }
    LevelSync sync;
    for (const LevelEntity& entity : from.entities) {
        if (!next.contains(entity.id)) {
            Destroy(entity.id);
            ++sync.destroyed;
            sync.models |= !entity.model.empty() || !entity.prefab.empty();
        }
    }
    ecs::World& world = m_scene->World();
    std::set<std::uint64_t> remade;
    for (const LevelEntity* entity : CreationOrder(to)) {
        const auto old = previous.find(entity->id);
        const ecs::Entity existing = Find(entity->id);
        if (old != previous.end() && !existing.IsNull()) {
            if (*old->second == *entity) {
                continue;
            }
            LevelEntity moved = *old->second;
            moved.transform = entity->transform;
            if (moved == *entity) {
                // Only the transform: move what is there.
                static_cast<math::Transform&>(world.Get<ecs::LocalTransform>(existing)) = entity->transform;
                ++sync.moved;
                continue;
            }
        }
        Make(*entity);
        remade.insert(entity->id);
        ++sync.created;
        sync.models |= !entity->model.empty() || !entity->prefab.empty() ||
                       (old != previous.end() && (!old->second->model.empty() || !old->second->prefab.empty()));
    }
    // An emitter on the surface of a model made again went with the old model: it is put back.
    for (const LevelEntity& entity : to.entities) {
        if (entity.particles && entity.particles->surface && remade.contains(entity.particles->surface->entity) &&
            !remade.contains(entity.id)) {
            Make(entity);
            ++sync.created;
        }
    }
    Link(to);
    return sync;
}

void LevelInstance::Create(const Level& level, const LevelEntity& entity) {
    Make(entity);
    Link(level);
}

void LevelInstance::Make(const LevelEntity& entity) {
    ecs::World& world = m_scene->World();
    AssetLibrary& assets = m_scene->Assets();

    // The children of the entity it replaces move to the new one.
    std::vector<ecs::Entity> children;
    if (const ecs::Entity old = Find(entity.id); !old.IsNull()) {
        for (ecs::Entity child = world.FirstChild(old); !child.IsNull(); child = world.NextSibling(child)) {
            if (world.Has<LevelEntityId>(child)) {
                children.push_back(child);
            }
        }
        for (const ecs::Entity child : children) {
            [[maybe_unused]] const core::Result<void> detached = world.SetParent(child, {});
        }
        Destroy(entity.id);
    }

    const ecs::Entity created = world.Create();
    m_entities[entity.id] = created;
    ecs::LocalTransform transform;
    static_cast<math::Transform&>(transform) = entity.transform;
    world.Add<ecs::LocalTransform>(created, transform);
    if (!m_part) {
        world.Add<LevelEntityId>(created, entity.id);
    }
    if (!entity.name.empty()) {
        world.Add<NodeName>(created, entity.name);
    }
    if (entity.parent != 0) {
        if (const ecs::Entity parent = Find(entity.parent); !parent.IsNull()) {
            [[maybe_unused]] const core::Result<void> attached = world.SetParent(created, parent);
        }
    }
    for (const ecs::Entity child : children) {
        [[maybe_unused]] const core::Result<void> attached = world.SetParent(child, created);
    }

    if (!entity.model.empty()) {
        core::Result<const Model*> model = assets.LoadModel(Resolve(entity.model));
        if (model) {
            if (core::Result<ecs::Entity> root = Instantiate(world, **model)) {
                [[maybe_unused]] const core::Result<void> attached = world.SetParent(*root, created);
                world.Add<ModelInstance>(created, *model, *root);
            }
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(model.error()));
        }
    }
    if (!entity.prefab.empty()) {
        Place(entity, created);
    }
    if (entity.light) {
        world.Add<Light>(created, *entity.light);
        // Its profile and cookie (ADR-0057): a light without them shines alike everywhere.
        LightTextures textures;
        if (!entity.light->profile.empty()) {
            if (core::Result<rhi::TextureHandle> profile = assets.LoadLightProfile(Resolve(entity.light->profile))) {
                textures.profile = *profile;
            } else {
                DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name,
                                core::Format(profile.error()));
            }
        }
        if (!entity.light->cookie.empty()) {
            if (core::Result<rhi::TextureHandle> cookie =
                    assets.LoadTexture(Resolve(entity.light->cookie), resources::TextureUse::Data)) {
                textures.cookie = *cookie;
            } else {
                DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name,
                                core::Format(cookie.error()));
            }
        }
        if (!textures.profile.IsNull() || !textures.cookie.IsNull()) {
            world.Add<LightTextures>(created, textures);
        }
    }
    if (entity.visibility) {
        world.Add<RayVisibility>(created, *entity.visibility);
    }
    if (entity.wet) {
        world.Add<WetVolume>(created, *entity.wet);
    }
    if (entity.cell) {
        world.Add<VisibilityCell>(created, *entity.cell);
    }
    if (entity.body && entity.body->mass <= 0.0f) {
        // A static box (ADR-0083), where the entity is in the world, parents included.
        glm::mat4 placement = math::ToMatrix(entity.transform);
        for (ecs::Entity above = world.Parent(created); !above.IsNull(); above = world.Parent(above)) {
            placement = math::ToMatrix(world.Get<ecs::LocalTransform>(above)) * placement;
        }
        StaticBox box;
        for (std::size_t corner = 0; corner < box.corners.size(); ++corner) {
            const glm::vec3 sign((corner & 1) != 0 ? 0.5f : -0.5f, (corner & 2) != 0 ? 0.5f : -0.5f,
                                 (corner & 4) != 0 ? 0.5f : -0.5f);
            box.corners[corner] = math::TransformPoint(placement, entity.body->center + sign * entity.body->size);
        }
        if (core::Result<physics::BodyId> body = m_scene->Physics().AddStaticMesh(box.corners, kBoxIndices)) {
            box.body = *body;
            world.Add<Body>(created, *entity.body);
            world.Add<StaticBox>(created, box);
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(body.error()));
        }
    } else if (entity.body) {
        // A loose object (ADR-0073): physics moves it from where the level puts it.
        const math::Transform& placed = entity.transform;
        const glm::vec3 half = glm::max(entity.body->size * placed.scale * 0.5f, glm::vec3(0.02f));
        math::Transform at = placed;
        at.scale = glm::vec3(1.0f);
        if (core::Result<physics::BodyId> body =
                m_scene->Physics().AddDynamicBox(half, entity.body->mass, at, entity.body->center * placed.scale)) {
            world.Add<Body>(created, *entity.body);
            world.Add<RigidBody>(created, *body);
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(body.error()));
        }
    }
    if (entity.trigger) {
        // Its sequence, from the level's directory to the asset root's (ADR-0071).
        Trigger trigger = *entity.trigger;
        trigger.sequence = Resolve(trigger.sequence);
        world.Add<Trigger>(created, std::move(trigger));
    }
    if (entity.decal) {
        world.Add<Decal>(created, *entity.decal);
        DecalTextures textures;
        if (core::Result<rhi::TextureHandle> albedo = assets.LoadTexture(Resolve(entity.decal->texture))) {
            textures.albedo = *albedo;
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(albedo.error()));
        }
        if (!entity.decal->normal.empty()) {
            if (core::Result<rhi::TextureHandle> normal =
                    assets.LoadTexture(Resolve(entity.decal->normal), resources::TextureUse::Normal)) {
                textures.normal = *normal;
            }
        }
        world.Add<DecalTextures>(created, textures);
    }
    if (entity.fog) {
        world.Add<FogVolume>(created, *entity.fog);
    }
    if (entity.wind) {
        world.Add<WindVolume>(created, *entity.wind);
    }
    if (entity.particles) {
        if (core::Result<ParticleEffectAsset> effect = assets.LoadParticleEffect(Resolve(entity.particles->effect))) {
            ParticleEmitter emitter{
                .effect = effect->effect, .emission = effect->emission, .enabled = entity.particles->enabled};
            if (entity.particles->burst) {
                emitter.emission.rate = 0.0f;
                emitter.emission.ratePerMeter = 0.0f;
                emitter.emission.bursts = {*entity.particles->burst};
            }
            ecs::Entity holder = created;
            if (const std::optional<LevelNodeRef>& surface = entity.particles->surface) {
                // On the node that has the triangles, which moves them.
                const ecs::Entity owner = Find(surface->entity);
                const ModelInstance* instance = owner.IsNull() ? nullptr : world.TryGet<ModelInstance>(owner);
                const ecs::Entity node =
                    instance != nullptr ? FindNodeEntity(world, instance->root, surface->node) : ecs::Entity{};
                emitter.surface = instance != nullptr ? SurfaceOf(*instance->model, surface->node) : nullptr;
                if (node.IsNull() || emitter.surface == nullptr) {
                    DAI_LOG_WARNING(kModule, "Level entity {} '{}': no triangles of node '{}' to emit on", entity.id,
                                    entity.name, surface->node);
                    emitter.surface = nullptr;
                } else {
                    holder = node;
                    m_surfaceEmitters[entity.id] = node;
                }
            }
            world.Add<ParticleEmitter>(holder, std::move(emitter));
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(effect.error()));
        }
    }
    if (entity.attractor) {
        ParticleAttractor attractor{.strength = entity.attractor->strength,
                                    .vortex = entity.attractor->vortex,
                                    .radius = entity.attractor->radius,
                                    .enabled = entity.attractor->enabled};
        if (!entity.attractor->effect.empty()) {
            if (core::Result<ParticleEffectAsset> effect =
                    assets.LoadParticleEffect(Resolve(entity.attractor->effect))) {
                attractor.effect = effect->effect;
            } else {
                DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name,
                                core::Format(effect.error()));
            }
        }
        world.Add<ParticleAttractor>(created, attractor);
    }
    if (entity.weather) {
        if (core::Result<ParticleEffectAsset> effect = assets.LoadParticleEffect(Resolve(entity.weather->effect))) {
            world.Add<WeatherZone>(created, WeatherZone{.effect = effect->effect,
                                                        .rate = entity.weather->rate,
                                                        .reach = entity.weather->reach,
                                                        .enabled = entity.weather->enabled});
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(effect.error()));
        }
    }
    if (entity.ribbon) {
        if (core::Result<std::shared_ptr<const RibbonEffect>> effect =
                assets.LoadRibbonEffect(Resolve(entity.ribbon->effect))) {
            world.Add<RibbonEmitter>(created, RibbonEmitter{.effect = *effect, .enabled = entity.ribbon->enabled});
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(effect.error()));
        }
    }
    if (entity.lightning) {
        Lightning lightning{.flash = entity.lightning->flash,
                            .interval = entity.lightning->interval,
                            .duration = entity.lightning->duration,
                            .enabled = entity.lightning->enabled};
        if (core::Result<std::shared_ptr<const RibbonEffect>> bolt =
                assets.LoadRibbonEffect(Resolve(entity.lightning->bolt))) {
            lightning.bolt = *bolt;
        } else {
            DAI_LOG_WARNING(kModule, "Level entity {} '{}': {}", entity.id, entity.name, core::Format(bolt.error()));
        }
        world.Add<Lightning>(created, std::move(lightning));
    }
    if (entity.game) {
        world.Add<Tags>(created, *entity.game);
    }
}

void LevelInstance::Link(const Level& level) {
    ecs::World& world = m_scene->World();
    for (const LevelEntity& entity : level.entities) {
        const ecs::Entity created = Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        if (entity.ribbon) {
            if (RibbonEmitter* ribbon = world.TryGet<RibbonEmitter>(created)) {
                ribbon->target = entity.ribbon->target != 0 ? Find(entity.ribbon->target) : ecs::Entity{};
            }
        }
        if (entity.lightning) {
            if (Lightning* lightning = world.TryGet<Lightning>(created)) {
                lightning->light = entity.lightning->light != 0 ? Find(entity.lightning->light) : ecs::Entity{};
            }
        }
    }
}

void LevelInstance::Destroy(std::uint64_t id) {
    ecs::World& world = m_scene->World();
    const ecs::Entity entity = Find(id);
    if (entity.IsNull()) {
        return;
    }
    // The bodies of loose objects under it, a prefab's among them, leave the physics world.
    std::vector<ecs::Entity> stack{entity};
    while (!stack.empty()) {
        const ecs::Entity current = stack.back();
        stack.pop_back();
        if (const RigidBody* body = world.TryGet<RigidBody>(current)) {
            m_scene->Physics().RemoveBody(body->body);
        }
        if (const StaticBox* box = world.TryGet<StaticBox>(current)) {
            m_scene->Physics().RemoveBody(box->body);
        }
        for (ecs::Entity child = world.FirstChild(current); !child.IsNull(); child = world.NextSibling(child)) {
            stack.push_back(child);
        }
    }
    world.Destroy(entity);
    // Its descendants made from the level went with it, and so do the emitters they put on other surfaces.
    std::erase_if(m_entities, [&](const auto& pair) {
        if (world.IsAlive(pair.second)) {
            return false;
        }
        m_prefabs.erase(pair.first);
        if (const auto emitter = m_surfaceEmitters.find(pair.first); emitter != m_surfaceEmitters.end()) {
            world.Remove<ParticleEmitter>(emitter->second);
            m_surfaceEmitters.erase(emitter);
        }
        return true;
    });
}

ecs::Entity LevelInstance::Find(std::uint64_t id) const {
    const auto found = m_entities.find(id);
    if (found == m_entities.end() || !m_scene->World().IsAlive(found->second)) {
        return {};
    }
    return found->second;
}

std::uint64_t LevelInstance::IdOf(ecs::Entity entity) const {
    const ecs::World& world = m_scene->World();
    for (; !entity.IsNull(); entity = world.Parent(entity)) {
        if (const LevelEntityId* id = world.TryGet<LevelEntityId>(entity)) {
            return id->value;
        }
    }
    return 0;
}

} // namespace daidalos::scene
