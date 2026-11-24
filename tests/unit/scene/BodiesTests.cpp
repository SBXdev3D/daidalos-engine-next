// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Bodies.hpp>
#include <daidalos/scene/Components.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace daidalos;
using namespace daidalos::scene;

namespace {

constexpr float kStep = 1.0f / 60.0f;

physics::BodyId AddFloor(physics::PhysicsWorld& physics) {
    const std::array<glm::vec3, 4> positions{glm::vec3(-20.0f, 0.0f, -20.0f), glm::vec3(-20.0f, 0.0f, 20.0f),
                                             glm::vec3(20.0f, 0.0f, 20.0f), glm::vec3(20.0f, 0.0f, -20.0f)};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    return physics.AddStaticMesh(positions, indices).value_or(physics::BodyId{});
}

} // namespace

TEST(Bodies, ShapesHaveTheirNames) {
    for (const physics::ShapeKind shape :
         {physics::ShapeKind::Box, physics::ShapeKind::Sphere, physics::ShapeKind::Cylinder,
          physics::ShapeKind::Capsule, physics::ShapeKind::Hull}) {
        EXPECT_EQ(BodyShapeOf(BodyShapeName(shape)), shape);
    }
    EXPECT_FALSE(BodyShapeOf("cone").has_value());
}

TEST(Bodies, SurfacesRubAndBounceAsTheyShould) {
    EXPECT_GT(SurfaceMaterial("rubber").restitution, SurfaceMaterial("wood").restitution);
    EXPECT_LT(SurfaceMaterial("ice").friction, 0.1f);
    EXPECT_EQ(SurfaceMaterial("cloth").restitution, 0.0f);
    // What is not known is wood.
    EXPECT_EQ(SurfaceMaterial("cheese"), SurfaceMaterial("wood"));
    EXPECT_EQ(SurfaceMaterial(""), SurfaceMaterial("wood"));
}

TEST(Bodies, AModelIsMadeOfItsMainSurfaceGlassLast) {
    Model lamp;
    lamp.parts.resize(2);
    lamp.parts[0].surface = "glass";
    lamp.parts[0].indices.assign(300, 0); // 100 triangles of glass.
    lamp.parts[1].surface = "metal";
    lamp.parts[1].indices.assign(30, 0); // 10 of metal.
    lamp.nodes.push_back(Model::Node{.name = "lamp", .parts = {0, 1}});
    EXPECT_EQ(MainSurface(lamp), "metal");
    lamp.nodes.front().parts = {0};
    EXPECT_EQ(MainSurface(lamp), "glass");
    EXPECT_EQ(MainSurface(Model{}), "");
}

TEST(Bodies, AModelsPointsAreWhereItsNodesPutThem) {
    Model model;
    model.parts.resize(1);
    model.parts[0].positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    Model::Node root{.name = "root", .parts = {}};
    root.local.translation = {0.0f, 2.0f, 0.0f};
    Model::Node child{.name = "child", .parent = 0, .parts = {0}};
    child.local.translation = {3.0f, 0.0f, 0.0f};
    model.nodes = {root, child};
    const std::vector<glm::vec3> points = ModelPoints(model);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0], glm::vec3(3.0f, 2.0f, 0.0f));
    EXPECT_EQ(points[1], glm::vec3(4.0f, 2.0f, 0.0f));
}

TEST(Bodies, HarderHitsAreLouderAndHeavierThingsLower) {
    EXPECT_EQ(HitVolume(0.1f), 0.0f);
    EXPECT_GT(HitVolume(2.0f), HitVolume(1.0f));
    EXPECT_EQ(HitVolume(50.0f), 1.0f);
    EXPECT_TRUE(IsHeavyHit(3.0f, 20.0f));
    EXPECT_FALSE(IsHeavyHit(3.0f, 2.0f));
    EXPECT_LT(HitPitch(100.0f), HitPitch(1.0f));
}

TEST(Bodies, ALandingIsHeardOnceAndRaisesDust) {
    // A 40 kg crate dropped from 2 m: one hit, not one a step while it settles, and dust, as it is heavy.
    ecs::World world;
    physics::PhysicsWorld physics;
    audio::SoundWorld sounds(nullptr, ".");
    AddFloor(physics);
    physics::DynamicBodyDesc desc;
    desc.shape.size = glm::vec3(0.6f);
    desc.mass = 40.0f;
    math::Transform start;
    start.translation = {0.0f, 2.3f, 0.0f};
    const physics::BodyId crate = physics.AddDynamicBody(desc, start).value();
    world.Add<RigidBody>(world.Create(), crate, std::string("wood"));

    PhysicsFeedback feedback;
    PhysicsSounds bank;
    // Clips that do not play: a SoundWorld without an engine plays nothing, but the feedback still counts.
    bank.lightHits["wood"] = {audio::ClipHandle{}};
    bank.heavyHits["wood"] = {audio::ClipHandle{}};
    bank.dust = renderer::ParticleEffectHandle(1, 1);
    feedback.SetSounds(bank);
    int dust = 0;
    const PhysicsFeedback::SpawnDust spawn = [&dust](const glm::vec3& position, const glm::vec3& direction,
                                                     std::uint32_t count) {
        ++dust;
        EXPECT_NEAR(position.y, 0.0f, 0.05f);
        EXPECT_GT(direction.y, 0.9f);
        EXPECT_GE(count, 2u);
    };
    for (int step = 0; step < 180; ++step) {
        physics.Step(kStep);
        feedback.Update(world, physics, sounds, spawn, kStep);
    }
    EXPECT_GE(feedback.HitCount(), 1u);
    EXPECT_LE(feedback.HitCount(), 3u); // A bounce or two at most.
    EXPECT_EQ(dust, 1);
}

TEST(Bodies, AGentleTouchIsSilent) {
    ecs::World world;
    physics::PhysicsWorld physics;
    audio::SoundWorld sounds(nullptr, ".");
    AddFloor(physics);
    physics::DynamicBodyDesc desc;
    desc.shape.size = glm::vec3(0.6f);
    math::Transform start;
    start.translation = {0.0f, 0.301f, 0.0f};
    const physics::BodyId crate = physics.AddDynamicBody(desc, start).value();
    world.Add<RigidBody>(world.Create(), crate, std::string("wood"));
    PhysicsFeedback feedback;
    PhysicsSounds bank;
    bank.lightHits["wood"] = {audio::ClipHandle{}};
    feedback.SetSounds(bank);
    for (int step = 0; step < 60; ++step) {
        physics.Step(kStep);
        feedback.Update(world, physics, sounds, {}, kStep);
    }
    EXPECT_EQ(feedback.HitCount(), 0u);
}

TEST(Bodies, ABreakingJointSnaps) {
    // ADR-0126: a 50 kg crate glued with a 200 N joint breaks at once, and is heard once.
    ecs::World world;
    physics::PhysicsWorld physics;
    audio::SoundWorld sounds(nullptr, ".");
    physics::DynamicBodyDesc desc;
    desc.mass = 50.0f;
    math::Transform start;
    start.translation = {0.0f, 3.0f, 0.0f};
    const physics::BodyId crate = physics.AddDynamicBody(desc, start).value();
    const physics::JointHandle glue = physics
                                          .AddJoint({.kind = physics::JointKind::Fixed,
                                                     .first = crate,
                                                     .anchor = {0.0f, 3.25f, 0.0f},
                                                     .breakForce = 200.0f})
                                          .value();
    const ecs::Entity entity = world.Create();
    world.Add<RigidBody>(entity, crate, std::string("wood"));
    world.Add<Joints>(entity, Joints{{glue}});
    PhysicsFeedback feedback;
    PhysicsSounds bank;
    bank.snaps["wood"] = audio::ClipHandle{};
    feedback.SetSounds(bank);
    for (int step = 0; step < 30; ++step) {
        physics.Step(kStep);
        feedback.Update(world, physics, sounds, {}, kStep);
    }
    EXPECT_TRUE(physics.IsBroken(glue));
    EXPECT_EQ(feedback.SnapCount(), 1u);
}
