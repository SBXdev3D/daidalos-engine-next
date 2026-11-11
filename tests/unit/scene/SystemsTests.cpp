// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <gtest/gtest.h>

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
    world.Add<scene::ParticleEmitter>(emitter, renderer::ParticleEffectHandle(1, 1), 30.0f);
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
    world.Add<scene::ParticleEmitter>(AddAt(world, {}), renderer::ParticleEffectHandle(1, 1), 1000.0f, false);
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 1.0f, spawns);
    EXPECT_TRUE(spawns.empty());
}

TEST(EmitParticles, SpawnsTurnWithTheirEntity) {
    ecs::World world;
    const ecs::Entity emitter = AddAt(world, {});
    world.Get<ecs::LocalTransform>(emitter).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world.Add<scene::ParticleEmitter>(emitter, renderer::ParticleEffectHandle(1, 1), 100.0f);
    ecs::PropagateTransforms(world);
    std::vector<renderer::ParticleSpawn> spawns;
    scene::EmitParticles(world, 0.1f, spawns);
    ASSERT_EQ(spawns.size(), 1U);
    // The effect's +Y, turned a quarter around X, points along +Z.
    EXPECT_NEAR(glm::distance(spawns[0].rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f,
                1e-5f);
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
