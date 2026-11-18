// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

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
