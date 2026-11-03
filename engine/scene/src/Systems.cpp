// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace daidalos::scene {

namespace {

void ExtractLights(ecs::World& world, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, Light>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const Light& light) {
            if (!light.enabled || light.intensity <= 0.0f || light.range <= 0.0f) {
                return;
            }
            view.lights.push_back({.type = light.type,
                                   .position = glm::vec3(transform.matrix[3]),
                                   .direction = glm::normalize(-glm::vec3(transform.matrix[2])),
                                   .color = light.color * light.intensity,
                                   .range = light.range,
                                   .falloff = light.falloff,
                                   .innerAngle = light.innerAngle,
                                   .outerAngle = light.outerAngle,
                                   .castsShadows = light.castsShadows});
        });
}

} // namespace

void ExtractScene(ecs::World& world, renderer::SceneView& view, ecs::Entity activeCamera) {
    DAI_PROFILE_SCOPE("scene::ExtractScene");
    view.camera.reset();
    view.draws.clear();
    view.jointMatrices.clear();
    view.lights.clear();

    bool chosen = false;
    std::uint32_t cameraIndex = std::numeric_limits<std::uint32_t>::max();
    world.Each<ecs::WorldTransform, Camera>([&](ecs::Entity entity, const ecs::WorldTransform& transform,
                                                const Camera& camera) {
        if (entity == activeCamera || (!chosen && entity.Index() < cameraIndex)) {
            chosen = (entity == activeCamera);
            cameraIndex = entity.Index();
            view.camera = renderer::CameraView{glm::inverse(transform.matrix), camera.verticalFov, camera.nearPlane};
        }
    });

    view.draws.reserve(world.Count<MeshRenderer>());
    world.Each<ecs::WorldTransform, MeshRenderer>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const MeshRenderer& component) {
            view.draws.push_back({.mesh = component.mesh,
                                  .world = transform.matrix,
                                  .material = component.material,
                                  .castsShadows = component.castsShadows});
        });

    world.Each<ecs::WorldTransform, SkinnedMeshRenderer>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const SkinnedMeshRenderer& component) {
            const auto firstJoint = static_cast<std::uint32_t>(view.jointMatrices.size());
            const std::vector<glm::mat4>* inverseBind = component.inverseBindMatrices.get();
            for (std::size_t index = 0; index < component.joints.size(); ++index) {
                const ecs::WorldTransform* joint = world.TryGet<ecs::WorldTransform>(component.joints[index]);
                const glm::mat4 bind =
                    (inverseBind != nullptr && index < inverseBind->size()) ? (*inverseBind)[index] : glm::mat4(1.0f);
                view.jointMatrices.push_back((joint != nullptr ? joint->matrix : transform.matrix) * bind);
            }
            view.draws.push_back({.mesh = component.mesh,
                                  .world = transform.matrix,
                                  .material = component.material,
                                  .firstJoint = component.joints.empty() ? renderer::kNoJoints : firstJoint,
                                  .castsShadows = component.castsShadows});
        });
    ExtractLights(world, view);
}

void EmitParticles(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns) {
    world.Each<ecs::WorldTransform, ParticleEmitter>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, ParticleEmitter& emitter) {
            if (!emitter.enabled || emitter.rate <= 0.0f) {
                emitter.pending = 0.0f;
                return;
            }
            emitter.pending += emitter.rate * delta;
            const float whole = std::floor(emitter.pending);
            emitter.pending -= whole;
            if (whole < 1.0f) {
                return;
            }
            const glm::mat3 axes(glm::normalize(glm::vec3(transform.matrix[0])),
                                 glm::normalize(glm::vec3(transform.matrix[1])),
                                 glm::normalize(glm::vec3(transform.matrix[2])));
            spawns.push_back({.effect = emitter.effect,
                              .position = glm::vec3(transform.matrix[3]),
                              .rotation = glm::quat_cast(axes),
                              .count = static_cast<std::uint32_t>(whole)});
        });
}

void SyncKinematicBodies(ecs::World& world, physics::PhysicsWorld& physics, float step) {
    world.Each<KinematicBody, ecs::LocalTransform>(
        [&](ecs::Entity /*entity*/, const KinematicBody& body, const ecs::LocalTransform& transform) {
            physics.MoveKinematic(body.id, transform.translation, transform.rotation, step);
        });
}

void SyncSoundEmitters(ecs::World& world, audio::SoundWorld& sounds) {
    world.Each<SoundEmitter, ecs::WorldTransform>(
        [&sounds](ecs::Entity /*entity*/, const SoundEmitter& emitter, const ecs::WorldTransform& transform) {
            sounds.SetPosition(emitter.voice, glm::vec3(transform.matrix[3]));
        });
}

} // namespace daidalos::scene
