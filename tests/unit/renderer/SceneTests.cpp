// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/Scene.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <gtest/gtest.h>

using namespace daidalos;
using renderer::Camera;
using renderer::MeshHandle;
using renderer::MeshRenderer;
using renderer::SceneView;

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
    renderer::ExtractScene(world, view);
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
    renderer::ExtractScene(world, view);

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
    renderer::ExtractScene(world, view);
    EXPECT_TRUE(view.draws.empty());
}

TEST(ExtractScene, ViewMatrixMovesTheWorldOppositeToTheCamera) {
    ecs::World world;
    const ecs::Entity camera = AddAt(world, {0.0f, 0.0f, 5.0f});
    world.Add<Camera>(camera);
    ecs::PropagateTransforms(world);

    SceneView view;
    renderer::ExtractScene(world, view);

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
    renderer::ExtractScene(world, view);

    ASSERT_TRUE(view.camera.has_value());
    EXPECT_FLOAT_EQ(view.camera->verticalFov, 0.5f);
}

TEST(ExtractScene, ReusedViewDropsThePreviousFrame) {
    ecs::World world;
    const ecs::Entity entity = AddAt(world, {0.0f, 0.0f, 0.0f});
    world.Add<MeshRenderer>(entity, MeshHandle(1, 1));
    world.Add<Camera>(entity);
    ecs::PropagateTransforms(world);

    SceneView view;
    renderer::ExtractScene(world, view);
    world.Destroy(entity);
    renderer::ExtractScene(world, view);

    EXPECT_FALSE(view.camera.has_value());
    EXPECT_TRUE(view.draws.empty());
}
