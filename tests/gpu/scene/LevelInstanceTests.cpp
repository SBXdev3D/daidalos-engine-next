// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/SaveGame.hpp>
#include <daidalos/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <vector>

using namespace daidalos;
using scene::Level;
using scene::LevelEntity;

namespace {

// The level sits beside the project's effects room, whose model has a crate that embers burn on.
constexpr const char* kLevelPath = "Factory/Test.level.json";

class LevelInstanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::Result<platform::Platform> created = platform::Platform::Create();
        if (!created) {
            GTEST_SKIP() << core::Format(created.error());
        }
        m_platform.emplace(std::move(*created));
        platform::WindowConfig config;
        config.width = 64;
        config.height = 64;
        config.vulkan = true;
        core::Result<platform::Window> window = platform::Window::Create(*m_platform, config);
        if (!window) {
            GTEST_SKIP() << core::Format(window.error());
        }
        m_window.emplace(std::move(*window));
        core::Result<renderer::Renderer> target = renderer::Renderer::Create(*m_window, {.validation = true});
        if (!target) {
            ASSERT_EQ(target.error().code, core::ErrorCode::NotSupported) << core::Format(target.error());
            GTEST_SKIP() << core::Format(target.error());
        }
        m_renderer.emplace(std::move(*target));
        m_scene = std::make_unique<scene::Scene>(*m_renderer, DAI_PROJECT_ASSET_DIR);
        m_instance = std::make_unique<scene::LevelInstance>(*m_scene, kLevelPath);
    }

    void TearDown() override {
        m_instance.reset();
        m_scene.reset();
        if (m_renderer.has_value()) {
            EXPECT_EQ(m_renderer->Device().ValidationErrorCount(), 0U);
        }
    }

    static LevelEntity Light(std::uint64_t id, std::uint64_t parent = 0) {
        LevelEntity entity{.id = id, .name = "Light", .parent = parent};
        entity.light = scene::Light{};
        return entity;
    }

    [[nodiscard]] std::size_t Count() const {
        std::size_t count = 0;
        m_scene->World().Each<scene::LevelEntityId>([&count](ecs::Entity, scene::LevelEntityId&) { ++count; });
        return count;
    }

    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
    std::optional<renderer::Renderer> m_renderer;
    std::unique_ptr<scene::Scene> m_scene;
    std::unique_ptr<scene::LevelInstance> m_instance;
};

TEST_F(LevelInstanceTest, SyncCreatesDestroysAndMoves) {
    Level from;
    from.entities = {Light(1), Light(2, 1), Light(3)};
    m_instance->Build(from);
    ASSERT_EQ(Count(), 3U);
    const ecs::Entity first = m_instance->Find(1);

    Level to = from;
    to.entities.erase(to.entities.begin() + 2); // 3 goes.
    to.entities[0].transform.translation = {1.0f, 2.0f, 3.0f};
    to.entities.push_back(Light(4, 2));
    const scene::LevelSync sync = m_instance->Sync(from, to);
    EXPECT_EQ(sync.created, 1U);
    EXPECT_EQ(sync.destroyed, 1U);
    EXPECT_EQ(sync.moved, 1U);
    EXPECT_FALSE(sync.models);

    EXPECT_EQ(Count(), 3U);
    EXPECT_TRUE(m_instance->Find(3).IsNull());
    EXPECT_EQ(m_instance->Find(1), first) << "A change of transform moves the entity, it is not made again";
    EXPECT_EQ(m_scene->World().Get<ecs::LocalTransform>(first).translation, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(m_scene->World().Parent(m_instance->Find(4)), m_instance->Find(2));

    // And back, as an undo does.
    m_instance->Sync(to, from);
    EXPECT_EQ(Count(), 3U);
    EXPECT_FALSE(m_instance->Find(3).IsNull());
    EXPECT_TRUE(m_instance->Find(4).IsNull());
    EXPECT_EQ(m_scene->World().Get<ecs::LocalTransform>(first).translation, glm::vec3(0.0f));
}

TEST_F(LevelInstanceTest, SyncMakesAChangedEntityAgainAndKeepsItsChildren) {
    Level from;
    from.entities = {Light(1), Light(2, 1)};
    m_instance->Build(from);
    const ecs::Entity child = m_instance->Find(2);

    Level to = from;
    to.entities[0].light->intensity = 42.0f;
    EXPECT_EQ(m_instance->Sync(from, to).created, 1U);
    EXPECT_FLOAT_EQ(m_scene->World().Get<scene::Light>(m_instance->Find(1)).intensity, 42.0f);
    EXPECT_EQ(m_instance->Find(2), child);
    EXPECT_EQ(m_scene->World().Parent(child), m_instance->Find(1));

    const scene::LevelSync nothing = m_instance->Sync(to, to);
    EXPECT_EQ(nothing.created + nothing.destroyed + nothing.moved, 0U) << "Nothing changed";
}

TEST_F(LevelInstanceTest, SyncMovesAnEntityToAnotherParent) {
    Level from;
    from.entities = {Light(1), Light(2), Light(3, 1)};
    m_instance->Build(from);
    Level to = from;
    to.entities[2].parent = 2;
    m_instance->Sync(from, to);
    EXPECT_EQ(m_scene->World().Parent(m_instance->Find(3)), m_instance->Find(2));
}

TEST_F(LevelInstanceTest, SurfaceEmittersFollowTheirModelAndGoWithTheirEntity) {
    Level from;
    LevelEntity room{.id = 1, .name = "Room", .model = "EffectsRoom.gltf"};
    LevelEntity embers{.id = 2, .name = "Embers"};
    embers.particles = scene::LevelParticles{.effect = "Effects/embers.effect.json",
                                             .surface = scene::LevelNodeRef{.entity = 1, .node = "burning_crate"}};
    // The emitter comes first in the file; the model it sits on is still made before it.
    from.entities = {embers, room};
    m_instance->Build(from);
    const auto emitters = [this] {
        std::size_t count = 0;
        m_scene->World().Each<scene::ParticleEmitter>([&count](ecs::Entity, scene::ParticleEmitter&) { ++count; });
        return count;
    };
    ASSERT_EQ(emitters(), 1U);

    // The room made again takes its old crate and its embers away: they are put back.
    Level moved = from;
    moved.entities[1].name = "Room again";
    const scene::LevelSync remade = m_instance->Sync(from, moved);
    EXPECT_TRUE(remade.models);
    EXPECT_EQ(remade.created, 2U) << "The room, and the embers on it";
    EXPECT_EQ(emitters(), 1U);

    // The embers' entity goes, and so do the embers on the crate.
    Level without = moved;
    without.entities.erase(without.entities.begin());
    m_instance->Sync(moved, without);
    EXPECT_EQ(emitters(), 0U);
}

TEST_F(LevelInstanceTest, SyncAppliesTheEnvironment) {
    Level from;
    m_instance->Build(from);
    Level to = from;
    to.environment.fogDensity = 0.25f;
    m_instance->Sync(from, to);
    EXPECT_FLOAT_EQ(m_scene->Atmosphere().fog.density, 0.25f);
}

} // namespace

TEST_F(LevelInstanceTest, SpawnedDecalsFadeAndKeepToTheirNumber) {
    const core::Result<rhi::TextureHandle> image = m_scene->Assets().LoadTexture("Factory/Decals/bullet_hole.png");
    ASSERT_TRUE(image.has_value()) << core::Format(image.error());
    for (std::size_t index = 0; index < scene::Scene::kMaxTransientDecals + 10; ++index) {
        m_scene->SpawnDecal({.position = glm::vec3(static_cast<float>(index), 0.0f, 0.0f),
                             .albedo = *image,
                             .lifetime = 10.0f,
                             .fade = 4.0f});
    }
    EXPECT_EQ(m_scene->TransientDecalCount(), scene::Scene::kMaxTransientDecals) << "The oldest go";
    m_scene->RunFrame(core::Seconds(8.0));
    renderer::SceneView view;
    m_scene->Extract(view);
    ASSERT_EQ(view.decals.size(), scene::Scene::kMaxTransientDecals);
    EXPECT_NEAR(view.decals.front().opacity, 0.5f, 1e-4f) << "Two seconds left of four of fading";
    EXPECT_NEAR(view.decals.front().world[3].x, 10.0f, 1e-4f) << "The first ten went";
    m_scene->RunFrame(core::Seconds(3.0));
    EXPECT_EQ(m_scene->TransientDecalCount(), 0U);
}

TEST_F(LevelInstanceTest, ALooseObjectFallsAndGoesWithItsEntity) {
    Level level;
    LevelEntity crate{.id = 1, .name = "Crate"};
    crate.transform.translation = {0.0f, 10.0f, 0.0f};
    crate.body = scene::Body{.size = glm::vec3(0.6f), .mass = 15.0f};
    level.entities.push_back(crate);
    m_instance->Build(level);
    const std::size_t bodies = m_scene->Physics().BodyCount();
    for (int step = 0; step < 30; ++step) {
        m_scene->RunFixedStep(core::Seconds(1.0 / 60.0));
    }
    const ecs::Entity made = m_instance->Find(1);
    ASSERT_FALSE(made.IsNull());
    EXPECT_LT(m_scene->World().Get<ecs::LocalTransform>(made).translation.y, 9.0f);
    m_instance->Sync(level, Level{});
    EXPECT_EQ(m_scene->Physics().BodyCount(), bodies - 1);
}

TEST_F(LevelInstanceTest, ALampHangsOnItsChainAndItsLightGoesWithIt) {
    // ADR-0126: three links from a hook at 6 m, a lamp at the end with a light under it, pushed: the lamp swings,
    // stays on its chain, and its light follows it.
    Level level;
    std::uint64_t above = 0;
    for (std::uint64_t link = 1; link <= 3; ++link) {
        LevelEntity entity{.id = link, .name = "Link"};
        entity.transform.translation = {0.0f, 5.9f - 0.2f * static_cast<float>(link - 1), 0.0f};
        entity.body = scene::Body{.size = {0.04f, 0.2f, 0.04f}, .mass = 0.5f, .surface = "metal"};
        entity.joints.push_back({.kind = physics::JointKind::Point, .to = above, .anchor = {0.0f, 0.1f, 0.0f}});
        level.entities.push_back(entity);
        above = link;
    }
    LevelEntity lamp{.id = 10, .name = "Lamp"};
    lamp.transform.translation = {0.0f, 5.2f, 0.0f};
    lamp.body = scene::Body{.size = glm::vec3(0.2f), .mass = 3.0f, .surface = "metal"};
    lamp.joints.push_back({.kind = physics::JointKind::Point, .to = 3, .anchor = {0.0f, 0.1f, 0.0f}});
    lamp.joints.push_back(
        {.kind = physics::JointKind::Distance, .toAnchor = {0.0f, 6.0f, 0.0f}, .limited = true, .highest = 0.8f});
    LevelEntity bulb{.id = 11, .name = "Bulb", .parent = 10};
    bulb.transform.translation = {0.0f, -0.15f, 0.0f};
    bulb.light = scene::Light{};
    level.entities.push_back(lamp);
    level.entities.push_back(bulb);
    m_instance->Build(level);
    EXPECT_EQ(m_scene->Physics().JointCount(), 5u);

    const ecs::Entity lampEntity = m_instance->Find(10);
    const scene::RigidBody& body = m_scene->World().Get<scene::RigidBody>(lampEntity);
    m_scene->Physics().SetBodyVelocity(body.body, {2.0f, 0.0f, 0.0f});
    float farthest = 0.0f;
    float widest = 0.0f;
    for (int step = 0; step < 120; ++step) {
        m_scene->RunFixedStep(core::Seconds(1.0 / 60.0));
        const glm::vec3 at = m_scene->Physics().BodyPosition(body.body);
        farthest = std::max(farthest, glm::distance(at, glm::vec3(0.0f, 6.0f, 0.0f)));
        widest = std::max(widest, std::abs(at.x));
    }
    EXPECT_LT(farthest, 0.85f);
    EXPECT_GT(widest, 0.2f) << "It swung";
    ecs::PropagateTransforms(m_scene->World());
    const glm::vec3 light(m_scene->World().Get<ecs::WorldTransform>(m_instance->Find(11)).matrix[3]);
    const glm::vec3 lampAt = m_scene->Physics().BodyPosition(body.body);
    EXPECT_LT(glm::distance(light, lampAt), 0.16f) << "The bulb hangs 15 cm under the lamp, wherever it is";

    // Made again with the level, the joints are not made twice.
    m_instance->Sync(level, level);
    EXPECT_EQ(m_scene->Physics().JointCount(), 5u);
    // A link that goes takes its joints, and the rest stay.
    Level shorter = level;
    std::erase_if(shorter.entities, [](const LevelEntity& entity) { return entity.id == 2; });
    m_instance->Sync(level, shorter);
    EXPECT_EQ(m_scene->Physics().JointCount(), 3u) << "Link 1 to the hook, the lamp to link 3, and the rope";
}

TEST_F(LevelInstanceTest, ABlastThrowsNearerThingsFasterAndNotThroughWalls) {
    // ADR-0127: four 10 kg crates around a blast at the origin of 5 m: at 1.5 m, at 3 m, at 7 m, and at 1.5 m
    // behind a wall.
    Level level;
    const auto crate = [&level](std::uint64_t id, glm::vec3 at) {
        LevelEntity entity{.id = id, .name = std::format("Crate {}", id)};
        entity.transform.translation = at;
        entity.body = scene::Body{.size = glm::vec3(0.4f), .mass = 10.0f};
        level.entities.push_back(entity);
    };
    crate(1, {1.5f, 0.0f, 0.0f});
    crate(2, {3.0f, 0.0f, 0.0f});
    crate(3, {7.0f, 0.0f, 0.0f});
    crate(4, {-1.5f, 0.0f, 0.0f});
    LevelEntity wall{.id = 5, .name = "Wall"};
    wall.transform.translation = {-0.8f, 0.0f, 0.0f};
    wall.body = scene::Body{.size = {0.2f, 4.0f, 4.0f}, .mass = 0.0f};
    level.entities.push_back(wall);
    m_instance->Build(level);
    const auto speed = [this](std::uint64_t id) {
        const physics::BodyId body = m_scene->World().Get<scene::RigidBody>(m_instance->Find(id)).body;
        const glm::vec3 velocity = m_scene->Physics().BodyVelocity(body);
        return glm::length(glm::vec2(velocity.x, velocity.z));
    };
    EXPECT_EQ(m_scene->Blast(glm::vec3(0.0f), 5.0f, 300.0f), 3u) << "The far crate is out of reach";
    EXPECT_GT(speed(1), speed(2));
    EXPECT_GT(speed(2), 0.5f);
    EXPECT_EQ(speed(3), 0.0f);
    EXPECT_LT(speed(4), speed(1) * 0.3f) << "Behind the wall";
    EXPECT_GT(speed(4), 0.0f) << "A blast wraps around corners";
    // A blast adds at most 25 m/s, however strong.
    const physics::BodyId nearest = m_scene->World().Get<scene::RigidBody>(m_instance->Find(1)).body;
    const glm::vec3 before = m_scene->Physics().BodyVelocity(nearest);
    m_scene->Blast(glm::vec3(0.0f), 5.0f, 1.0e6f);
    EXPECT_NEAR(glm::distance(m_scene->Physics().BodyVelocity(nearest), before), 25.0f, 0.01f);
}

TEST_F(LevelInstanceTest, SequencesPushReleaseBreakAndDrive) {
    // ADR-0127: a crate pushed, a crate held until released, a sign whose joint is broken, a wheel driven.
    Level level;
    LevelEntity pushed{.id = 1, .name = "Pushed"};
    pushed.transform.translation = {0.0f, 5.0f, 0.0f};
    pushed.body = scene::Body{.size = glm::vec3(0.4f), .mass = 10.0f, .held = true};
    LevelEntity held{.id = 2, .name = "Held"};
    held.transform.translation = {3.0f, 5.0f, 0.0f};
    held.body = scene::Body{.size = glm::vec3(0.4f), .mass = 10.0f, .held = true};
    LevelEntity sign{.id = 3, .name = "Sign"};
    sign.transform.translation = {6.0f, 5.0f, 0.0f};
    sign.body = scene::Body{.size = glm::vec3(0.4f), .mass = 5.0f};
    sign.joints.push_back({.kind = physics::JointKind::Fixed});
    LevelEntity wheel{.id = 4, .name = "Wheel"};
    wheel.transform.translation = {9.0f, 5.0f, 0.0f};
    wheel.body = scene::Body{.size = {1.0f, 0.2f, 0.2f}, .mass = 5.0f};
    wheel.joints.push_back({.kind = physics::JointKind::Hinge, .axis = {0.0f, 0.0f, 1.0f}});
    level.entities = {pushed, held, sign, wheel};
    m_instance->Build(level);
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(R"({"cues": [
        {"at": 0.0, "impulse": {"entity": "Pushed", "impulse": [0, 0, -30]}},
        {"at": 0.5, "release": {"entity": "Held"}},
        {"at": 0.0, "break": {"entity": "Sign", "joint": 0}},
        {"at": 0.0, "motor": {"entity": "Wheel", "joint": 0, "mode": "speed", "target": 3}}
    ]})");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    m_scene->PlaySequence(std::make_shared<const scene::Sequence>(*sequence));
    const auto body = [this](std::uint64_t id) {
        return m_scene->World().Get<scene::RigidBody>(m_instance->Find(id)).body;
    };
    for (int step = 0; step < 15; ++step) {
        m_scene->RunFrame(core::Seconds(1.0 / 60.0));
        m_scene->RunFixedStep(core::Seconds(1.0 / 60.0));
    }
    EXPECT_LT(m_scene->Physics().BodyPosition(body(1)).z, -0.4f) << "Pushed, and let go to fly";
    EXPECT_NEAR(m_scene->Physics().BodyPosition(body(2)).y, 5.0f, 1e-3f) << "Still held";
    EXPECT_LT(m_scene->Physics().BodyPosition(body(3)).y, 4.8f) << "Its glue broken, it falls";
    for (int step = 0; step < 45; ++step) {
        m_scene->RunFrame(core::Seconds(1.0 / 60.0));
        m_scene->RunFixedStep(core::Seconds(1.0 / 60.0));
    }
    EXPECT_LT(m_scene->Physics().BodyPosition(body(2)).y, 4.8f) << "Released at 0.5 s";
    EXPECT_NEAR(std::abs(m_scene->Physics().BodyAngularVelocity(body(4)).z), 3.0f, 0.2f);
}

TEST_F(LevelInstanceTest, ABrokenJointStaysBrokenInASavedGame) {
    Level level;
    LevelEntity crate{.id = 1, .name = "Crate"};
    crate.transform.translation = {0.0f, 3.0f, 0.0f};
    crate.body = scene::Body{.size = glm::vec3(0.5f), .mass = 10.0f};
    crate.joints.push_back({.kind = physics::JointKind::Fixed, .anchor = {0.0f, 0.25f, 0.0f}});
    level.entities.push_back(crate);
    m_instance->Build(level);
    m_scene->World().Add<scene::SaveKey>(m_instance->Find(1), std::string("crate"));
    const physics::JointHandle joint = m_scene->World().Get<scene::Joints>(m_instance->Find(1)).handles.at(0);
    m_scene->Physics().BreakJoint(joint);
    scene::SaveData saved;
    scene::SaveScene(*m_scene, saved);

    m_instance->Sync(level, Level{});
    m_instance->Sync(Level{}, level);
    m_scene->World().Add<scene::SaveKey>(m_instance->Find(1), std::string("crate"));
    const physics::JointHandle again = m_scene->World().Get<scene::Joints>(m_instance->Find(1)).handles.at(0);
    EXPECT_FALSE(m_scene->Physics().IsBroken(again));
    scene::LoadScene(*m_scene, saved);
    EXPECT_TRUE(m_scene->Physics().IsBroken(again));
}

TEST_F(LevelInstanceTest, ALooseObjectIsMadeOfItsModelsSurfaceAndCanWrapIt) {
    // ADR-0125: the crate's materials say wood; a hull of the crate rests about where its box would.
    Level level;
    LevelEntity crate{.id = 1, .name = "Crate", .model = "Props/crate.gltf"};
    crate.transform.translation = {0.0f, 1.0f, 0.0f};
    crate.body = scene::Body{.shape = physics::ShapeKind::Hull, .mass = 15.0f};
    LevelEntity ball{.id = 2, .name = "Ball"};
    ball.transform.translation = {3.0f, 1.0f, 0.0f};
    ball.body = scene::Body{.shape = physics::ShapeKind::Sphere, .size = glm::vec3(0.4f), .surface = "rubber"};
    level.entities = {crate, ball};
    m_instance->Build(level);
    const scene::RigidBody* crateBody = m_scene->World().TryGet<scene::RigidBody>(m_instance->Find(1));
    const scene::RigidBody* ballBody = m_scene->World().TryGet<scene::RigidBody>(m_instance->Find(2));
    ASSERT_NE(crateBody, nullptr);
    ASSERT_NE(ballBody, nullptr);
    EXPECT_EQ(crateBody->surface, "wood");
    EXPECT_EQ(ballBody->surface, "rubber");
    const std::vector<physics::BodyOutline> outlines = m_scene->Physics().Outlines({0.0f, 1.0f, 0.0f}, 1.0f);
    ASSERT_FALSE(outlines.empty());
    EXPECT_FALSE(outlines.front().lines.empty());
}

TEST_F(LevelInstanceTest, APrefabPlacesItsEntitiesAsPartsOfOne) {
    Level level;
    level.entities.push_back(LevelEntity{.id = 5, .name = "Alarm", .prefab = "Prefabs/alarm_beams.prefab.json"});
    m_instance->Build(level);
    const auto lights = [this] {
        std::vector<ecs::Entity> found;
        m_scene->World().Each<scene::Light>([&found](ecs::Entity entity, scene::Light&) { found.push_back(entity); });
        return found;
    };
    ASSERT_EQ(lights().size(), 2U) << "The prefab's two beams";
    EXPECT_EQ(Count(), 1U) << "Its parts are not entities of the level";
    for (const ecs::Entity beam : lights()) {
        EXPECT_EQ(m_scene->World().Parent(beam), m_instance->Find(5));
        EXPECT_EQ(m_instance->IdOf(beam), 5U) << "Clicking a part selects what places it";
    }

    Level moved = level;
    moved.entities[0].transform.translation = {0.0f, 3.0f, 0.0f};
    EXPECT_EQ(m_instance->Sync(level, moved).moved, 1U);
    EXPECT_EQ(lights().size(), 2U);
    EXPECT_EQ(m_instance->RemakePrefabs(moved), 1U);
    EXPECT_EQ(lights().size(), 2U) << "Made again, not twice";

    m_instance->Sync(moved, Level{});
    EXPECT_TRUE(lights().empty());
}

TEST_F(LevelInstanceTest, AMissingPrefabLeavesTheEntityAlone) {
    Level level;
    level.entities.push_back(LevelEntity{.id = 1, .prefab = "Prefabs/missing.prefab.json"});
    m_instance->Build(level);
    EXPECT_FALSE(m_instance->Find(1).IsNull());
}

TEST_F(LevelInstanceTest, LooseObjectsUnderAnEntityGoWithIt) {
    Level level;
    level.entities.push_back(Light(1));
    LevelEntity crate{.id = 2, .name = "Crate", .parent = 1};
    crate.body = scene::Body{.size = glm::vec3(0.6f), .mass = 15.0f};
    level.entities.push_back(crate);
    const std::size_t bodies = m_scene->Physics().BodyCount();
    m_instance->Build(level);
    EXPECT_EQ(m_scene->Physics().BodyCount(), bodies + 1);
    m_instance->Destroy(1);
    EXPECT_EQ(m_scene->Physics().BodyCount(), bodies);
}

TEST_F(LevelInstanceTest, TheSceneFindsSoundsUnderItsAssetRoot) {
    // The scene once gave its sounds an empty root (its members were initialized in another order than written),
    // and the game played none.
    const core::Result<audio::ClipHandle> clip = m_scene->Sounds().Load("Factory/Sounds/alarm.wav");
    if (!clip) {
        EXPECT_NE(clip.error().code, core::ErrorCode::NotFound) << core::Format(clip.error());
    }
}

TEST_F(LevelInstanceTest, AModelsMaterialChangesInPlace) {
    core::Result<const scene::Model*> model = m_scene->Assets().LoadModel("Factory/Props/crate.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    const scene::Model& crate = **model;
    ASSERT_FALSE(crate.materials.empty());
    for (const scene::Model::Part& part : crate.parts) {
        ASSERT_TRUE(part.materialIndex.has_value());
        EXPECT_EQ(part.material, crate.materials[*part.materialIndex].handle);
    }
    resources::MaterialData values = crate.materials[0].values;
    const rhi::TextureHandle texture = crate.materials[0].desc.baseColorTexture;
    values.roughnessFactor = 0.123f;
    values.baseColorFactor = {1.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(m_scene->Assets().UpdateMaterial(crate, 0, values));
    EXPECT_FLOAT_EQ(crate.materials[0].desc.roughnessFactor, 0.123f);
    EXPECT_EQ(crate.materials[0].desc.baseColorTexture, texture) << "Its textures stay";
    EXPECT_FLOAT_EQ(crate.materials[0].values.roughnessFactor, 0.123f);
    EXPECT_FALSE(m_scene->Assets().UpdateMaterial(crate, 99, values));
}

TEST_F(LevelInstanceTest, AMasslessBodyIsAStaticBoxWhereItsEntityIs) {
    Level level;
    LevelEntity group{.id = 1, .name = "Room"};
    group.transform.translation = {10.0f, 0.0f, 0.0f};
    LevelEntity crate{.id = 2, .name = "Crate", .parent = 1};
    crate.transform.translation = {0.0f, 0.5f, 0.0f};
    crate.body = scene::Body{.size = glm::vec3(1.0f), .mass = 0.0f};
    level.entities = {group, crate};
    const std::size_t bodies = m_scene->Physics().BodyCount();
    m_instance->Build(level);
    EXPECT_EQ(m_scene->Physics().BodyCount(), bodies + 1);
    const std::optional<physics::RayHit> hit =
        m_scene->Physics().CastRay(glm::vec3(10.0f, 0.5f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f), 10.0f);
    ASSERT_TRUE(hit.has_value()) << "The box stands where its parent puts it";
    EXPECT_NEAR(hit->position.z, 0.5f, 1e-3f);
    for (int step = 0; step < 10; ++step) {
        m_scene->RunFixedStep(core::Seconds(1.0 / 60.0));
    }
    EXPECT_EQ(m_scene->World().Get<ecs::LocalTransform>(m_instance->Find(2)).translation, glm::vec3(0.0f, 0.5f, 0.0f))
        << "Nothing moves it";
    m_instance->Destroy(1);
    EXPECT_EQ(m_scene->Physics().BodyCount(), bodies);
}
