// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace daidalos;
using renderer::MeshHandle;
using renderer::SceneView;
using scene::Camera;
using scene::MeshRenderer;

namespace {

ecs::Entity AddAt(ecs::World& world, const glm::vec3& position) {
    const ecs::Entity entity = world.Create();
    ecs::LocalTransform transform;
    transform.translation = position;
    world.Add<ecs::LocalTransform>(entity, transform);
    return entity;
}

} // namespace

TEST(ExtractScene, WithoutEntitiesTheViewIsEmpty) {
    ecs::World world;
    SceneView view;
    scene::ExtractScene(world, view);
    EXPECT_FALSE(view.camera.has_value());
    EXPECT_TRUE(view.draws.empty());
}

TEST(ExtractScene, CopiesTheWorldMatrixOfMeshes) {
    ecs::World world;
    const ecs::Entity parent = AddAt(world, {1.0f, 0.0f, 0.0f});
    const ecs::Entity child = AddAt(world, {0.0f, 2.0f, 0.0f});
    ASSERT_TRUE(world.SetParent(child, parent).has_value());
    const MeshHandle mesh(3, 1);
    const renderer::MaterialHandle material(4, 1);
    world.Add<MeshRenderer>(child, mesh, material);
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);

    ASSERT_EQ(view.draws.size(), 1U);
    EXPECT_EQ(view.draws[0].mesh, mesh);
    EXPECT_EQ(view.draws[0].material, material);
    EXPECT_EQ(glm::vec3(view.draws[0].world[3]), glm::vec3(1.0f, 2.0f, 0.0f));
}

TEST(ExtractScene, MeshWithoutWorldTransformIsNotDrawn) {
    ecs::World world;
    const ecs::Entity entity = world.Create();
    world.Add<MeshRenderer>(entity, MeshHandle(1, 1));

    SceneView view;
    scene::ExtractScene(world, view);
    EXPECT_TRUE(view.draws.empty());
}

TEST(ExtractScene, ActiveCameraWinsOverTheLowestIndex) {
    ecs::World world;
    const auto addCamera = [&world](float x) {
        const ecs::Entity entity = world.Create();
        ecs::LocalTransform transform;
        transform.translation = {x, 0.0f, 0.0f};
        world.Add<ecs::LocalTransform>(entity, transform);
        world.Add<scene::Camera>(entity);
        return entity;
    };
    addCamera(1.0f);
    const ecs::Entity active = addCamera(2.0f);
    addCamera(3.0f);
    ecs::PropagateTransforms(world);

    renderer::SceneView view;
    scene::ExtractScene(world, view, active);
    ASSERT_TRUE(view.camera.has_value());
    EXPECT_FLOAT_EQ(view.camera->view[3].x, -2.0f);

    scene::ExtractScene(world, view);
    EXPECT_FLOAT_EQ(view.camera->view[3].x, -1.0f);

    world.Destroy(active);
    scene::ExtractScene(world, view, active);
    EXPECT_FLOAT_EQ(view.camera->view[3].x, -1.0f) << "A stale camera falls back on the lowest index";
}

TEST(ExtractScene, ViewMatrixMovesTheWorldOppositeToTheCamera) {
    ecs::World world;
    const ecs::Entity camera = AddAt(world, {0.0f, 0.0f, 5.0f});
    world.Add<Camera>(camera);
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);

    ASSERT_TRUE(view.camera.has_value());
    const glm::vec4 origin = view.camera->view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(origin.z, -5.0f); // In front of a camera that looks down -Z.
}

TEST(ExtractScene, LowestEntityIndexWinsAmongCameras) {
    ecs::World world;
    const ecs::Entity first = AddAt(world, {0.0f, 0.0f, 1.0f});
    world.Add<Camera>(first, Camera{0.5f, 0.1f});
    const ecs::Entity second = AddAt(world, {0.0f, 0.0f, 2.0f});
    world.Add<Camera>(second, Camera{1.5f, 0.1f});
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);

    ASSERT_TRUE(view.camera.has_value());
    EXPECT_EQ(view.camera->verticalFov, 0.5f);
}

TEST(ExtractScene, ReusedViewDropsThePreviousFrame) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {0.0f, 0.0f, 0.0f});
    world.Add<MeshRenderer>(entity, MeshHandle(1, 1));
    world.Add<Camera>(entity);
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);
    world.Destroy(entity);
    scene::ExtractScene(world, view);

    EXPECT_FALSE(view.camera.has_value());
    EXPECT_TRUE(view.draws.empty());
}

TEST(ExtractScene, SkinnedMeshGetsTheMatricesOfItsJoints) {
    ecs::World world;
    const ecs::Entity body = AddAt(world, {100.0f, 0.0f, 0.0f});
    const ecs::Entity hip = AddAt(world, {1.0f, 0.0f, 0.0f});
    const ecs::Entity hand = AddAt(world, {0.0f, 2.0f, 0.0f});
    ASSERT_TRUE(world.SetParent(hand, hip).has_value());
    auto inverseBind = std::make_shared<const std::vector<glm::mat4>>(
        std::vector<glm::mat4>{glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f))});
    world.Add<scene::SkinnedMeshRenderer>(body, MeshHandle(3, 1), renderer::MaterialHandle{},
                                          std::vector<ecs::Entity>{hip, hand}, inverseBind);
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);

    ASSERT_EQ(view.draws.size(), 1U);
    EXPECT_EQ(view.draws[0].firstJoint, 0U);
    ASSERT_EQ(view.jointMatrices.size(), 2U);
    EXPECT_EQ(glm::vec3(view.jointMatrices[0][3]), glm::vec3(1.0f, 0.0f, 0.0f));
    // The hand is at (1, 2, 0), and its bind pose one meter up.
    EXPECT_EQ(glm::vec3(view.jointMatrices[1][3]), glm::vec3(1.0f, 1.0f, 0.0f));
}

TEST(ExtractScene, MissingJointFollowsTheSkinnedEntity) {
    ecs::World world;
    const ecs::Entity body = AddAt(world, {5.0f, 0.0f, 0.0f});
    const ecs::Entity gone = world.Create();
    world.Destroy(gone);
    world.Add<scene::SkinnedMeshRenderer>(body, MeshHandle(3, 1), renderer::MaterialHandle{},
                                          std::vector<ecs::Entity>{gone}, nullptr);
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);

    ASSERT_EQ(view.jointMatrices.size(), 1U);
    EXPECT_EQ(glm::vec3(view.jointMatrices[0][3]), glm::vec3(5.0f, 0.0f, 0.0f));
}

TEST(ExtractScene, RigidMeshHasNoJoints) {
    ecs::World world;
    world.Add<MeshRenderer>(AddAt(world, {0.0f, 0.0f, 0.0f}), MeshHandle(3, 1));
    ecs::PropagateTransforms(world);
    SceneView view;
    view.jointMatrices.resize(4);
    scene::ExtractScene(world, view);
    EXPECT_EQ(view.draws.at(0).firstJoint, renderer::kNoJoints);
    EXPECT_TRUE(view.jointMatrices.empty());
}

TEST(SyncKinematicBodies, MovesBodiesToTheirEntities) {
    constexpr float kStep = 1.0f / 60.0f;
    physics::PhysicsWorld physics;
    core::Result<physics::BodyId> box = physics.AddKinematicBox({1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(box.has_value());
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {1.0f, 2.0f, 3.0f});
    world.Add<scene::KinematicBody>(entity, *box);

    scene::SyncKinematicBodies(world, physics, kStep);
    physics.Step(kStep);

    const glm::vec3 position = physics.BodyPosition(*box);
    EXPECT_NEAR(position.x, 1.0f, 0.001f);
    EXPECT_NEAR(position.y, 2.0f, 0.001f);
    EXPECT_NEAR(position.z, 3.0f, 0.001f);
}

TEST(ExtractScene, LightsTakeTheirPlaceAndDirectionFromTheirEntity) {
    ecs::World world;
    const ecs::Entity lamp = AddAt(world, {1.0f, 3.0f, 2.0f});
    // Turned to look down: a spot light shines down its entity's local -Z.
    world.Get<ecs::LocalTransform>(lamp).rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world.Add<scene::Light>(lamp, scene::Light{.type = renderer::LightType::Spot,
                                               .color = {0.5f, 0.25f, 1.0f},
                                               .intensity = 2.0f,
                                               .range = 7.0f,
                                               .radius = 0.1f});
    world.Add<scene::Light>(AddAt(world, {}), scene::Light{.enabled = false});
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.lights.size(), 1U);
    const renderer::Light& light = view.lights[0];
    EXPECT_EQ(light.type, renderer::LightType::Spot);
    EXPECT_NEAR(glm::distance(light.position, glm::vec3(1.0f, 3.0f, 2.0f)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(light.direction, glm::vec3(0.0f, -1.0f, 0.0f)), 0.0f, 1e-5f);
    EXPECT_EQ(light.color, glm::vec3(1.0f, 0.5f, 2.0f));
    EXPECT_EQ(light.range, 7.0f);
    EXPECT_EQ(light.radius, 0.1f);
}

TEST(ExtractScene, WindTurnsWithItsEntityButIsNotScaledByIt) {
    ecs::World world;
    const ecs::Entity draft = AddAt(world, {1.0f, 2.0f, 3.0f});
    ecs::LocalTransform& transform = world.Get<ecs::LocalTransform>(draft);
    transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = glm::vec3(4.0f);
    world.Add<scene::WindVolume>(draft, scene::WindVolume{.velocity = {2.0f, 0.0f, 0.0f}, .gust = 0.5f});
    world.Add<scene::WindVolume>(AddAt(world, {}), scene::WindVolume{.enabled = false});
    ecs::PropagateTransforms(world);
    renderer::SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.windVolumes.size(), 1U);
    // +X turned a quarter around +Y is -Z.
    EXPECT_NEAR(glm::distance(view.windVolumes[0].velocity, glm::vec3(0.0f, 0.0f, -2.0f)), 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(view.windVolumes[0].gust, 0.5f);
    EXPECT_EQ(glm::vec3(view.windVolumes[0].world[3]), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(ExtractScene, AttractorsTurnAroundTheirEntitysY) {
    ecs::World world;
    const ecs::Entity lamp = AddAt(world, {0.0f, 3.0f, 0.0f});
    world.Get<ecs::LocalTransform>(lamp).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world.Add<scene::ParticleAttractor>(
        lamp,
        scene::ParticleAttractor{.effect = renderer::ParticleEffectHandle(2, 1), .strength = 3.0f, .vortex = 1.0f});
    ecs::PropagateTransforms(world);
    renderer::SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.particleAttractors.size(), 1U);
    const renderer::ParticleAttractor& attractor = view.particleAttractors[0];
    EXPECT_EQ(attractor.position, glm::vec3(0.0f, 3.0f, 0.0f));
    EXPECT_NEAR(glm::distance(attractor.axis, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f, 1e-5f);
    EXPECT_EQ(attractor.effect, renderer::ParticleEffectHandle(2, 1));
    EXPECT_FLOAT_EQ(attractor.strength, 3.0f);
    EXPECT_FLOAT_EQ(attractor.vortex, 1.0f);
}

TEST(ExtractScene, FogVolumesAreBoxesAtTheirEntity) {
    ecs::World world;
    const ecs::Entity dust = AddAt(world, {2.0f, 1.0f, -3.0f});
    world.Get<ecs::LocalTransform>(dust).scale = glm::vec3(4.0f, 2.0f, 6.0f);
    world.Add<scene::FogVolume>(dust, scene::FogVolume{.density = 0.5f,
                                                       .albedo = {0.7f, 0.8f, 0.9f},
                                                       .falloffHeight = 0.5f,
                                                       .noise = 0.5f,
                                                       .noiseVelocity = {0.0f, 1.0f, 0.0f},
                                                       .glow = {0.0f, 0.2f, 0.05f}});
    world.Add<scene::FogVolume>(AddAt(world, {}), scene::FogVolume{.enabled = false});
    world.Add<scene::FogVolume>(AddAt(world, {}), scene::FogVolume{.density = 0.0f});
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.fogVolumes.size(), 1U) << "Disabled and empty boxes are left out";
    const renderer::FogVolume& volume = view.fogVolumes[0];
    // The unit cube, scaled to the box's size, at the entity.
    EXPECT_NEAR(glm::distance(glm::vec3(volume.world * glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)), glm::vec3(4.0f, 2.0f, 0.0f)),
                0.0f, 1e-5f);
    EXPECT_EQ(volume.density, 0.5f);
    EXPECT_EQ(volume.albedo, glm::vec3(0.7f, 0.8f, 0.9f));
    EXPECT_EQ(volume.falloffHeight, 0.5f);
    EXPECT_EQ(volume.noise, 0.5f);
    EXPECT_EQ(volume.noiseVelocity, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(volume.glow, glm::vec3(0.0f, 0.2f, 0.05f));

    scene::ExtractScene(world, view);
    EXPECT_EQ(view.fogVolumes.size(), 1U) << "A view reused for the next frame starts over";
}

TEST(ExtractScene, LightsAndDrawsAreNamedByTheirEntity) {
    ecs::World world;
    const ecs::Entity lamp = AddAt(world, {});
    world.Add<scene::Light>(lamp, scene::Light{});
    const ecs::Entity panel = AddAt(world, {});
    world.Add<MeshRenderer>(panel, MeshHandle(1, 1), renderer::MaterialHandle(1, 1));
    ecs::PropagateTransforms(world);

    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.lights.size(), 1U);
    ASSERT_EQ(view.draws.size(), 1U);
    EXPECT_NE(view.lights[0].id, 0U);
    EXPECT_NE(view.draws[0].id, 0U);
    EXPECT_NE(view.lights[0].id, view.draws[0].id);
    // Another extraction names them the same.
    SceneView again;
    scene::ExtractScene(world, again);
    EXPECT_EQ(again.lights[0].id, view.lights[0].id);
    EXPECT_EQ(again.draws[0].id, view.draws[0].id);
}

TEST(ExtractScene, ATemperatureColorsTheLightButKeepsItsBrightness) {
    // A warm bulb (ADR-0043): its color times the black body's at 2700 K, whose luminance is 1.
    ecs::World world;
    world.Add<scene::Light>(AddAt(world, {}), scene::Light{.intensity = 100.0f, .temperature = 2700.0f});
    ecs::PropagateTransforms(world);
    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.lights.size(), 1U);
    const glm::vec3 color = view.lights[0].color;
    EXPECT_GT(color.r, color.g);
    EXPECT_GT(color.g, color.b);
    EXPECT_NEAR(0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b, 100.0f, 0.01f);
}

TEST(ExtractScene, LightsAreTheSizeOfABulbUnlessToldOtherwise) {
    ecs::World world;
    world.Add<scene::Light>(AddAt(world, {}), scene::Light{});
    ecs::PropagateTransforms(world);
    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.lights.size(), 1U);
    EXPECT_EQ(view.lights[0].radius, 0.05f);
}

TEST(EmitParticles, RateAccumulatesAcrossFrames) {
    ecs::World world;
    const ecs::Entity emitter = AddAt(world, {1.0f, 2.0f, 3.0f});
    world.Add<scene::ParticleEmitter>(emitter, renderer::ParticleEffectHandle(1, 1),
                                      scene::ParticleEmission{.rate = 30.0f});
    ecs::PropagateTransforms(world);

    std::vector<renderer::ParticleSpawn> spawns;
    std::uint32_t total = 0;
    for (int frame = 0; frame < 60; ++frame) {
        spawns.clear();
        scene::EmitParticles(world, 1.0f / 60.0f, spawns);
        for (const renderer::ParticleSpawn& spawn : spawns) {
            total += spawn.count;
            EXPECT_EQ(spawn.position, glm::vec3(1.0f, 2.0f, 3.0f));
        }
    }
    // 30 per second for one second, give or take the fraction still pending.
    EXPECT_GE(total, 29U);
    EXPECT_LE(total, 30U);
}

TEST(EmitParticles, DisabledEmitterEmitsNothing) {
    ecs::World world;
    world.Add<scene::ParticleEmitter>(AddAt(world, {}), renderer::ParticleEffectHandle(1, 1),
                                      scene::ParticleEmission{.rate = 1000.0f}, false);
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 1.0f, spawns);
    EXPECT_TRUE(spawns.empty());
}

TEST(EmitParticles, SpawnsTurnWithTheirEntity) {
    ecs::World world;
    const ecs::Entity emitter = AddAt(world, {});
    world.Get<ecs::LocalTransform>(emitter).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world.Add<scene::ParticleEmitter>(emitter, renderer::ParticleEffectHandle(1, 1),
                                      scene::ParticleEmission{.rate = 100.0f});
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.1f, spawns);
    ASSERT_EQ(spawns.size(), 1U);
    // The effect's +Y, turned a quarter around X, points along +Z.
    EXPECT_NEAR(glm::distance(spawns[0].rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f,
                1e-5f);
}

namespace {

// The particles an emitter of the given emission spawns in a frame of delta seconds.
std::uint32_t Emitted(ecs::World& world, float delta) {
    std::vector<renderer::ParticleSpawn> spawns;
    ecs::PropagateTransforms(world);
    scene::EmitParticles(world, delta, spawns);
    std::uint32_t total = 0;
    for (const renderer::ParticleSpawn& spawn : spawns) {
        total += spawn.count;
    }
    return total;
}

} // namespace

TEST(EmitParticles, EmitsPerMeterSpreadAlongTheWay) {
    ecs::World world;
    const ecs::Entity emitter = AddAt(world, {});
    world.Add<scene::ParticleEmitter>(emitter, renderer::ParticleEffectHandle(1, 1),
                                      scene::ParticleEmission{.rate = 0.0f, .ratePerMeter = 10.0f});
    EXPECT_EQ(Emitted(world, 0.1f), 0U) << "nowhere to come from on the first frame";
    world.Get<ecs::LocalTransform>(emitter).translation = {2.0f, 0.0f, 0.0f};
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.5f, spawns);
    ASSERT_EQ(spawns.size(), 1U);
    EXPECT_EQ(spawns[0].count, 20U);
    EXPECT_EQ(spawns[0].position, glm::vec3(2.0f, 0.0f, 0.0f));
    ASSERT_TRUE(spawns[0].previousPosition.has_value());
    EXPECT_EQ(*spawns[0].previousPosition, glm::vec3(0.0f));
    EXPECT_EQ(spawns[0].velocity, glm::vec3(4.0f, 0.0f, 0.0f));
}

TEST(EmitParticles, BurstsFireOnTheirSchedule) {
    ecs::World world;
    world.Add<scene::ParticleEmitter>(
        AddAt(world, {}), renderer::ParticleEffectHandle(1, 1),
        scene::ParticleEmission{
            .rate = 0.0f,
            .bursts = {{.time = 0.0f, .count = 5}, {.time = 1.0f, .count = 2, .interval = 0.5f, .repeats = 3}}});
    EXPECT_EQ(Emitted(world, 0.5f), 5U) << "the first burst, at once";
    EXPECT_EQ(Emitted(world, 0.4f), 0U);
    EXPECT_EQ(Emitted(world, 0.2f), 2U) << "the second, at 1 s";
    EXPECT_EQ(Emitted(world, 1.0f), 4U) << "at 1.5 and 2 s, the last of its three";
    EXPECT_EQ(Emitted(world, 10.0f), 0U);
}

TEST(EmitParticles, EnabledAgainItStartsOver) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {});
    world.Add<scene::ParticleEmitter>(entity, renderer::ParticleEffectHandle(1, 1),
                                      scene::ParticleEmission{.rate = 0.0f, .bursts = {{.time = 0.0f, .count = 3}}});
    EXPECT_EQ(Emitted(world, 0.1f), 3U);
    EXPECT_EQ(Emitted(world, 0.1f), 0U);
    world.Get<scene::ParticleEmitter>(entity).enabled = false;
    EXPECT_EQ(Emitted(world, 0.1f), 0U);
    world.Get<scene::ParticleEmitter>(entity).enabled = true;
    EXPECT_EQ(Emitted(world, 0.1f), 3U);
}

TEST(ExtractEmitterLights, AnEmitterLightsAsItEmitsAndFadesAfter) {
    ecs::World world;
    const ecs::Entity fire = AddAt(world, {1.0f, 0.0f, 0.0f});
    scene::ParticleEmission emission{.rate = 10.0f};
    emission.light = scene::EmitterLight{.color = {1.0f, 0.5f, 0.25f},
                                         .intensity = 20.0f,
                                         .range = 6.0f,
                                         .radius = 0.2f,
                                         .offset = {0.0f, 0.5f, 0.0f},
                                         .fade = 1.0f};
    world.Add<scene::ParticleEmitter>(fire, renderer::ParticleEffectHandle(1, 1), emission);
    const auto lights = [&](float delta) {
        std::vector<renderer::ParticleSpawn> spawns;
        renderer::SceneView view;
        ecs::PropagateTransforms(world);
        scene::EmitParticles(world, delta, spawns);
        scene::ExtractEmitterLights(world, view);
        return view.lights;
    };
    std::vector<renderer::Light> lit = lights(0.1f);
    ASSERT_EQ(lit.size(), 1U);
    EXPECT_EQ(lit[0].type, renderer::LightType::Point);
    EXPECT_EQ(lit[0].position, glm::vec3(1.0f, 0.5f, 0.0f)) << "at its offset";
    EXPECT_NEAR(glm::distance(lit[0].color, glm::vec3(20.0f, 10.0f, 5.0f)), 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(lit[0].range, 6.0f);
    EXPECT_FLOAT_EQ(lit[0].radius, 0.2f);
    EXPECT_NE(lit[0].id, 0U);

    world.Get<scene::ParticleEmitter>(fire).enabled = false;
    lit = lights(1.0f);
    ASSERT_EQ(lit.size(), 1U);
    EXPECT_NEAR(lit[0].color.r, 20.0f / std::exp(1.0f), 1e-3f) << "fading over a second";
    EXPECT_TRUE(lights(20.0f).empty()) << "out";
}

TEST(MakeBolt, SplitsALineBetweenItsEnds) {
    const std::vector<std::vector<glm::vec3>> bolt = scene::MakeBolt(5, 0.15f, 0.0f, 3);
    ASSERT_EQ(bolt.size(), 1U) << "no branches";
    const std::vector<glm::vec3>& points = bolt.front();
    ASSERT_EQ(points.size(), 33U) << "2^5 segments";
    EXPECT_EQ(points.front(), glm::vec3(0.0f));
    EXPECT_EQ(points.back(), glm::vec3(1.0f, 0.0f, 0.0f));
    bool moved = false;
    for (std::size_t i = 0; i < points.size(); ++i) {
        // Middles move aside only: along the line, the points are evenly spread.
        EXPECT_NEAR(points[i].x, static_cast<float>(i) / 32.0f, 1e-5f);
        // At most the first displacement, plus half of it, a quarter...
        EXPECT_LE(std::abs(points[i].y), 0.3f);
        EXPECT_LE(std::abs(points[i].z), 0.3f);
        moved = moved || points[i].y != 0.0f;
    }
    EXPECT_TRUE(moved);
}

TEST(MakeBolt, TheSameSeedMakesTheSameBolt) {
    EXPECT_EQ(scene::MakeBolt(6, 0.2f, 0.3f, 11), scene::MakeBolt(6, 0.2f, 0.3f, 11));
    EXPECT_NE(scene::MakeBolt(6, 0.2f, 0.3f, 11), scene::MakeBolt(6, 0.2f, 0.3f, 12));
}

TEST(MakeBolt, BranchesStartOnTheBolt) {
    const std::vector<std::vector<glm::vec3>> bolt = scene::MakeBolt(6, 0.2f, 1.0f, 5);
    ASSERT_GT(bolt.size(), 1U);
    for (std::size_t branch = 1; branch < bolt.size(); ++branch) {
        ASSERT_GE(bolt[branch].size(), 3U);
        EXPECT_NE(std::ranges::find(bolt.front(), bolt[branch].front()), bolt.front().end());
    }
}

namespace {

// A ribbon emitter of an effect, at a place, reaching a target if given.
ecs::Entity AddRibbon(ecs::World& world, const scene::RibbonEffect& effect, const glm::vec3& position,
                      ecs::Entity target = {}) {
    const ecs::Entity entity = AddAt(world, position);
    world.Add<scene::RibbonEmitter>(
        entity, scene::RibbonEmitter{.effect = std::make_shared<const scene::RibbonEffect>(effect), .target = target});
    return entity;
}

SceneView Ribbons(ecs::World& world, float delta) {
    SceneView view;
    ecs::PropagateTransforms(world);
    scene::ExtractRibbons(world, delta, view);
    return view;
}

} // namespace

TEST(ExtractRibbons, ALineGoesFromItsEmitterToItsTarget) {
    ecs::World world;
    scene::RibbonEffect effect;
    effect.color = {{0.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}, {1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)}};
    effect.width = {{0.0f, 0.1f}, {1.0f, 0.3f}};
    effect.tiling = 2.0f;
    const ecs::Entity target = AddAt(world, {4.0f, 0.0f, 0.0f});
    AddRibbon(world, effect, {0.0f, 0.0f, 0.0f}, target);
    const SceneView view = Ribbons(world, 0.1f);
    ASSERT_EQ(view.ribbons.size(), 1U);
    const std::vector<renderer::RibbonPoint>& points = view.ribbons[0].points;
    ASSERT_GE(points.size(), 2U);
    EXPECT_EQ(points.front().position, glm::vec3(0.0f));
    EXPECT_NEAR(glm::distance(points.back().position, glm::vec3(4.0f, 0.0f, 0.0f)), 0.0f, 1e-5f);
    EXPECT_EQ(points.front().color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(points.back().color, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(points.front().width, 0.1f);
    EXPECT_FLOAT_EQ(points.back().width, 0.3f);
    EXPECT_FLOAT_EQ(points.back().u - points.front().u, 2.0f) << "4 m at 2 m per repeat";
}

TEST(ExtractRibbons, WithoutATargetALineGoesUpItsEmitter) {
    ecs::World world;
    scene::RibbonEffect effect;
    effect.length = 3.0f;
    AddRibbon(world, effect, {1.0f, 0.0f, 0.0f});
    const SceneView view = Ribbons(world, 0.1f);
    ASSERT_EQ(view.ribbons.size(), 1U);
    EXPECT_NEAR(glm::distance(view.ribbons[0].points.back().position, glm::vec3(1.0f, 3.0f, 0.0f)), 0.0f, 1e-5f);
}

TEST(ExtractRibbons, ABoltKeepsItsShapeUntilItsRefresh) {
    ecs::World world;
    scene::RibbonEffect effect;
    effect.shape = scene::RibbonShape::Bolt;
    effect.generations = 4;
    effect.displacement = 0.2f;
    effect.refresh = 0.1f;
    AddRibbon(world, effect, {0.0f, 0.0f, 0.0f}, AddAt(world, {0.0f, 0.0f, -2.0f}));
    const SceneView first = Ribbons(world, 0.01f);
    ASSERT_EQ(first.ribbons.size(), 1U);
    EXPECT_EQ(first.ribbons[0].points.size(), 17U);
    const SceneView same = Ribbons(world, 0.02f);
    EXPECT_EQ(same.ribbons[0].points[8].position, first.ribbons[0].points[8].position);
    const SceneView next = Ribbons(world, 0.2f);
    EXPECT_NE(next.ribbons[0].points[8].position, first.ribbons[0].points[8].position);
    EXPECT_NEAR(glm::distance(next.ribbons[0].points.back().position, glm::vec3(0.0f, 0.0f, -2.0f)), 0.0f, 1e-5f);
}

TEST(ExtractRibbons, ATrailFollowsWhereItsEmitterWent) {
    ecs::World world;
    scene::RibbonEffect effect;
    effect.shape = scene::RibbonShape::Trail;
    effect.duration = 1.0f;
    effect.spacing = 0.1f;
    const ecs::Entity wisp = AddAt(world, {0.0f, 0.0f, 0.0f});
    world.Add<scene::RibbonEmitter>(
        wisp, scene::RibbonEmitter{.effect = std::make_shared<const scene::RibbonEffect>(effect)});
    EXPECT_TRUE(Ribbons(world, 0.1f).ribbons.empty()) << "one point is no ribbon";
    for (int step = 1; step <= 5; ++step) {
        world.Get<ecs::LocalTransform>(wisp).translation = {static_cast<float>(step), 0.0f, 0.0f};
        Ribbons(world, 0.1f);
    }
    world.Get<ecs::LocalTransform>(wisp).translation = {6.0f, 0.0f, 0.0f};
    SceneView view = Ribbons(world, 0.1f);
    ASSERT_EQ(view.ribbons.size(), 1U);
    EXPECT_EQ(view.ribbons[0].points.front().position, glm::vec3(6.0f, 0.0f, 0.0f)) << "newest first";
    EXPECT_EQ(view.ribbons[0].points.size(), 7U);
    // A second on, the old points are gone.
    for (int step = 0; step < 12; ++step) {
        view = Ribbons(world, 0.1f);
    }
    EXPECT_TRUE(view.ribbons.empty() || view.ribbons[0].points.size() <= 2U);
}

TEST(ExtractRibbons, ADisabledRibbonShowsNothingAndItsLightFades) {
    ecs::World world;
    scene::RibbonEffect effect;
    effect.light = scene::EmitterLight{.intensity = 10.0f, .range = 3.0f, .fade = 1.0f};
    const ecs::Entity arc = AddRibbon(world, effect, {0.0f, 0.0f, 0.0f});
    const SceneView lit = Ribbons(world, 0.1f);
    ASSERT_EQ(lit.ribbons.size(), 1U);
    ASSERT_EQ(lit.lights.size(), 1U);
    EXPECT_NEAR(glm::distance(lit.lights[0].position, glm::vec3(0.0f, 0.5f, 0.0f)), 0.0f, 1e-5f) << "at its middle";
    world.Get<scene::RibbonEmitter>(arc).enabled = false;
    const SceneView off = Ribbons(world, 1.0f);
    EXPECT_TRUE(off.ribbons.empty());
    ASSERT_EQ(off.lights.size(), 1U);
    EXPECT_NEAR(off.lights[0].color.r, 10.0f / std::exp(1.0f), 1e-3f);
}

namespace {

// A camera at a place, looking along -Z.
scene::EmissionView ViewFrom(const glm::vec3& eye, float density = 1.0f) {
    return {.density = density, .camera = glm::translate(glm::mat4(1.0f), eye), .fieldOfView = glm::radians(90.0f)};
}

std::uint32_t Count(const std::vector<renderer::ParticleSpawn>& spawns) {
    std::uint32_t total = 0;
    for (const renderer::ParticleSpawn& spawn : spawns) {
        total += spawn.count;
    }
    return total;
}

} // namespace

TEST(MaySee, WithinReachAndViewOnly) {
    const scene::EmissionView view = ViewFrom(glm::vec3(0.0f));
    EXPECT_TRUE(scene::MaySee(view, {0.0f, 0.0f, -10.0f}, 1.0f, 50.0f)) << "ahead";
    EXPECT_FALSE(scene::MaySee(view, {0.0f, 0.0f, -60.0f}, 1.0f, 50.0f)) << "too far";
    EXPECT_FALSE(scene::MaySee(view, {0.0f, 0.0f, 10.0f}, 1.0f, 50.0f)) << "behind";
    EXPECT_TRUE(scene::MaySee(view, {0.0f, 0.0f, 1.5f}, 2.0f, 50.0f)) << "behind, but its particles reach the camera";
    EXPECT_TRUE(scene::MaySee(view, {12.0f, 0.0f, -10.0f}, 1.0f, 50.0f)) << "at the edge of a wide view";
    EXPECT_TRUE(scene::MaySee({}, {0.0f, 0.0f, 100.0f}, 1.0f, 50.0f)) << "without a camera";
}

TEST(EmitParticles, AnEmitterOutOfViewEmitsNothingButKeepsItsLight) {
    ecs::World world;
    const ecs::Entity behind = AddAt(world, {0.0f, 0.0f, 20.0f});
    scene::ParticleEmission emission{.rate = 100.0f};
    emission.light = scene::EmitterLight{.intensity = 5.0f};
    world.Add<scene::ParticleEmitter>(behind, renderer::ParticleEffectHandle(1, 1), emission);
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.1f, spawns, ViewFrom(glm::vec3(0.0f)));
    EXPECT_TRUE(spawns.empty());
    EXPECT_FLOAT_EQ(world.Get<scene::ParticleEmitter>(behind).activity, 1.0f);
    scene::EmitParticles(world, 0.1f, spawns, ViewFrom({0.0f, 0.0f, 25.0f}));
    EXPECT_EQ(Count(spawns), 10U) << "in view again";
}

TEST(EmitParticles, DensityScalesRatesAndBurstsKeepOne) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, glm::vec3(0.0f));
    scene::ParticleEmission emission{.rate = 100.0f};
    emission.bursts = {{.time = 0.0f, .count = 1}, {.time = 0.0f, .count = 40}};
    world.Add<scene::ParticleEmitter>(entity, renderer::ParticleEffectHandle(1, 1), emission);
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.1f, spawns, {.density = 0.5f});
    EXPECT_EQ(Count(spawns), 5U + 1U + 20U);
}

TEST(EmitParticles, ASurfaceEmitsOnItsTrianglesAlongTheirNormal) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {0.0f, 1.0f, 0.0f});
    // A square of 2 m lying flat, facing up, and a sliver of no area.
    const std::vector<glm::vec3> positions{
        {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}, {5.0f, 0.0f, 0.0f}};
    const std::vector<std::uint32_t> indices{0, 2, 1, 0, 3, 2, 4, 4, 4};
    std::shared_ptr<const scene::MeshSurface> surface = scene::MakeMeshSurface(positions, indices);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->cumulativeArea.size(), 2U) << "no triangle without area";
    EXPECT_FLOAT_EQ(surface->cumulativeArea.back(), 4.0f);
    world.Add<scene::ParticleEmitter>(entity, scene::ParticleEmitter{.effect = renderer::ParticleEffectHandle(1, 1),
                                                                     .emission = {.rate = 200.0f},
                                                                     .surface = surface});
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.1f, spawns);
    ASSERT_EQ(spawns.size(), 20U) << "one spawn per particle";
    glm::vec3 mean(0.0f);
    for (const renderer::ParticleSpawn& spawn : spawns) {
        EXPECT_EQ(spawn.count, 1U);
        EXPECT_NEAR(spawn.position.y, 1.0f, 1e-5f);
        EXPECT_LE(std::abs(spawn.position.x), 1.0f + 1e-5f);
        EXPECT_LE(std::abs(spawn.position.z), 1.0f + 1e-5f);
        EXPECT_NEAR(glm::distance(spawn.rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f,
                    1e-4f)
            << "leaving along the normal";
        mean += spawn.position;
    }
    mean /= static_cast<float>(spawns.size());
    EXPECT_LT(std::abs(mean.x), 0.5f) << "spread over the square";
    EXPECT_EQ(scene::MakeMeshSurface(positions, std::vector<std::uint32_t>{4, 4, 4}), nullptr);
}

TEST(EmitWeather, OnlyOverThePartOfTheZoneWithinReach) {
    ecs::World world;
    const ecs::Entity zone = world.Create();
    ecs::LocalTransform transform;
    transform.translation = {0.0f, 10.0f, 0.0f};
    transform.scale = {100.0f, 4.0f, 100.0f};
    world.Add<ecs::LocalTransform>(zone, transform);
    world.Add<scene::WeatherZone>(
        zone, scene::WeatherZone{.effect = renderer::ParticleEffectHandle(1, 1), .rate = 10.0f, .reach = 5.0f});
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitWeather(world, 0.1f, spawns, ViewFrom({0.0f, 1.5f, 0.0f}));
    ASSERT_EQ(spawns.size(), 1U);
    // 10 m by 10 m within reach, 10 a square meter a second, for a tenth of a second.
    EXPECT_EQ(spawns[0].count, 100U);
    EXPECT_EQ(spawns[0].position, glm::vec3(0.0f, 12.0f, 0.0f)) << "at the zone's top, over the camera";
    EXPECT_EQ(spawns[0].scale, glm::vec3(5.0f, 1.0f, 5.0f));

    spawns.clear();
    scene::EmitWeather(world, 0.1f, spawns, ViewFrom({48.0f, 1.5f, 0.0f}));
    ASSERT_EQ(spawns.size(), 1U);
    EXPECT_EQ(spawns[0].count, 70U) << "7 m of the camera's 10 are in the zone";
    EXPECT_FLOAT_EQ(spawns[0].position.x, 46.5f);

    spawns.clear();
    scene::EmitWeather(world, 0.1f, spawns, ViewFrom({80.0f, 1.5f, 0.0f}));
    EXPECT_TRUE(spawns.empty()) << "out of reach";
    scene::EmitWeather(world, 0.1f, spawns, {});
    EXPECT_TRUE(spawns.empty()) << "no camera";
}

TEST(StrikeBrightness, FlashesThenFlickersThenStops) {
    const glm::vec2 flickers{0.1f, 0.25f};
    EXPECT_FLOAT_EQ(scene::StrikeBrightness(0.0f, flickers, 0.5f), 1.0f);
    EXPECT_LT(scene::StrikeBrightness(0.09f, flickers, 0.5f), 0.2f);
    EXPECT_FLOAT_EQ(scene::StrikeBrightness(0.1f, flickers, 0.5f), 1.0f);
    EXPECT_FLOAT_EQ(scene::StrikeBrightness(0.6f, flickers, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(scene::StrikeBrightness(-0.1f, flickers, 0.5f), 0.0f);
}

TEST(UpdateLightning, StrikesFlashesTheLightAndThundersLater) {
    ecs::World world;
    const ecs::Entity moon = AddAt(world, glm::vec3(0.0f));
    world.Add<scene::Light>(moon, scene::Light{.type = renderer::LightType::Directional, .intensity = 1.0f});
    const ecs::Entity storm = world.Create();
    ecs::LocalTransform box;
    box.translation = {0.0f, 20.0f, -343.0f};
    box.scale = {10.0f, 20.0f, 1.0f};
    world.Add<ecs::LocalTransform>(storm, box);
    scene::RibbonEffect bolt;
    bolt.shape = scene::RibbonShape::Bolt;
    world.Add<scene::Lightning>(storm, scene::Lightning{.bolt = std::make_shared<const scene::RibbonEffect>(bolt),
                                                        .light = moon,
                                                        .flash = 10.0f,
                                                        .interval = {1.0f, 1.0f}});
    const scene::EmissionView camera = ViewFrom(glm::vec3(0.0f));
    std::vector<scene::Thunder> thunder;
    const auto frame = [&](float delta) {
        SceneView view;
        ecs::PropagateTransforms(world);
        scene::ExtractScene(world, view);
        scene::UpdateLightning(world, delta, view, camera, thunder);
        return view;
    };
    SceneView view = frame(0.5f);
    ASSERT_EQ(view.lights.size(), 1U);
    const glm::vec3 calm = view.lights[0].color;
    EXPECT_TRUE(view.ribbons.empty()) << "not yet";
    view = frame(0.6f);
    ASSERT_EQ(view.lights.size(), 1U);
    EXPECT_NEAR(view.lights[0].color.r, calm.r * 10.0f, 1e-3f) << "the flash";
    ASSERT_FALSE(view.ribbons.empty());
    EXPECT_GT(view.ribbons[0].points.front().position.y, view.ribbons[0].points.back().position.y)
        << "from the top down";
    view = frame(0.6f);
    EXPECT_EQ(view.lights[0].color, calm) << "over";
    EXPECT_TRUE(thunder.empty()) << "sound takes a second to come 343 m";
    for (int step = 0; step < 6; ++step) {
        frame(0.1f);
    }
    ASSERT_FALSE(thunder.empty());
    EXPECT_NEAR(thunder[0].distance, 343.0f, 30.0f);
}

TEST(Flicker, StaysBetweenOneAndOneLessItsDepth) {
    float lowest = 1.0f;
    float highest = 0.0f;
    for (int step = 0; step < 1000; ++step) {
        const float value = scene::Flicker(8.0f, 0.4f, static_cast<float>(step) * 0.01f, 7);
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
    }
    EXPECT_GE(lowest, 0.6f - 1e-5f);
    EXPECT_LE(highest, 1.0f + 1e-5f);
    EXPECT_LT(lowest, 0.75f) << "it does flicker";
    EXPECT_FLOAT_EQ(scene::Flicker(0.0f, 0.4f, 1.0f, 7), 1.0f) << "no rate, no flicker";
}

TEST(BurstsBetween, CountsTheFiringsInAHalfOpenSpan) {
    const scene::ParticleBurst once{.time = 1.0f};
    EXPECT_EQ(scene::BurstsBetween(once, 0.0f, 1.0f), 0U) << "the end is not included";
    EXPECT_EQ(scene::BurstsBetween(once, 1.0f, 2.0f), 1U) << "the start is";
    const scene::ParticleBurst forever{.time = 0.0f, .interval = 0.25f};
    EXPECT_EQ(scene::BurstsBetween(forever, 0.0f, 1.0f), 4U);
    EXPECT_EQ(scene::BurstsBetween(forever, 100.0f, 101.0f), 4U);
    EXPECT_EQ(scene::BurstsBetween(forever, 1.0f, 1.0f), 0U);
}

TEST(ExtractScene, AMeshDrawnForTheFirstTimeHasNotMoved) {
    ecs::World world;
    world.Add<MeshRenderer>(AddAt(world, {1.0f, 0.0f, 0.0f}), MeshHandle(3, 1));
    ecs::PropagateTransforms(world);
    SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.draws.size(), 1U);
    ASSERT_TRUE(view.draws[0].previousWorld.has_value());
    EXPECT_EQ(*view.draws[0].previousWorld, view.draws[0].world);
}

TEST(ExtractScene, AMovedMeshGivesWhereItWasInTheLastView) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {1.0f, 0.0f, 0.0f});
    world.Add<MeshRenderer>(entity, MeshHandle(3, 1));
    ecs::PropagateTransforms(world);
    SceneView view;
    scene::ExtractScene(world, view);

    world.Get<ecs::LocalTransform>(entity).translation = {4.0f, 0.0f, 0.0f};
    ecs::PropagateTransforms(world);
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.draws.size(), 1U);
    EXPECT_EQ(glm::vec3(view.draws[0].world[3]), glm::vec3(4.0f, 0.0f, 0.0f));
    EXPECT_EQ(glm::vec3((*view.draws[0].previousWorld)[3]), glm::vec3(1.0f, 0.0f, 0.0f));

    scene::ExtractScene(world, view);
    EXPECT_EQ(*view.draws[0].previousWorld, view.draws[0].world) << "it stood still since";
}

TEST(ExtractScene, AJointSharedByTwoSkinsGivesBothTheSamePreviousMatrix) {
    ecs::World world;
    const ecs::Entity hip = AddAt(world, {1.0f, 0.0f, 0.0f});
    for (int body = 0; body < 2; ++body) {
        world.Add<scene::SkinnedMeshRenderer>(AddAt(world, {0.0f, 0.0f, 0.0f}), MeshHandle(3, 1),
                                              renderer::MaterialHandle{}, std::vector<ecs::Entity>{hip}, nullptr);
    }
    ecs::PropagateTransforms(world);
    SceneView view;
    scene::ExtractScene(world, view);

    world.Get<ecs::LocalTransform>(hip).translation = {2.0f, 0.0f, 0.0f};
    ecs::PropagateTransforms(world);
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.previousJointMatrices.size(), 2U);
    for (const glm::mat4& previous : view.previousJointMatrices) {
        EXPECT_EQ(glm::vec3(previous[3]), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    EXPECT_EQ(glm::vec3(view.jointMatrices[1][3]), glm::vec3(2.0f, 0.0f, 0.0f));
}
