// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/LightUnits.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace daidalos::scene {

namespace {

/// An entity's index and generation, plus one, since 0 names nothing: what names it as long as it lives.
renderer::SceneId SceneIdOf(ecs::Entity entity) {
    return ((std::uint64_t{entity.Generation()} << 32U) | entity.Index()) + 1;
}

void ExtractLights(ecs::World& world, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, Light>(
        [&](ecs::Entity entity, const ecs::WorldTransform& transform, const Light& light) {
            if (!light.enabled || light.intensity <= 0.0f || light.range <= 0.0f) {
                return;
            }
            view.lights.push_back(
                {.type = light.type,
                 .position = glm::vec3(transform.matrix[3]),
                 .direction = glm::normalize(-glm::vec3(transform.matrix[2])),
                 .color = light.color *
                          (light.temperature > 0.0f ? renderer::ColorTemperature(light.temperature) : glm::vec3(1.0f)) *
                          light.intensity,
                 .range = light.range,
                 .innerAngle = light.innerAngle,
                 .outerAngle = light.outerAngle,
                 .radius = light.radius,
                 .angularRadius = light.angularRadius,
                 .id = SceneIdOf(entity)});
        });
}

void ExtractFogVolumes(ecs::World& world, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, FogVolume>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const FogVolume& volume) {
            if (!volume.enabled || volume.density <= 0.0f) {
                return;
            }
            view.fogVolumes.push_back({.world = transform.matrix,
                                       .density = volume.density,
                                       .albedo = volume.albedo,
                                       .edge = volume.edge,
                                       .noise = volume.noise,
                                       .noiseFrequency = volume.noiseFrequency,
                                       .noiseVelocity = volume.noiseVelocity});
        });
}

/// The matrices of the entities drawn this frame, kept once the extraction is done: an entity can be
/// read several times in one extraction, as a joint of two skins, and must give the same previous matrix.
class DrawnTransforms {
public:
    explicit DrawnTransforms(ecs::World& world) : m_world(&world) {}

    /// The matrix the entity had when the last view was extracted, or this one if it was not drawn.
    glm::mat4 Previous(ecs::Entity entity, const glm::mat4& current) {
        m_drawn.emplace_back(entity, current);
        const DrawnTransform* drawn = m_world->TryGet<DrawnTransform>(entity);
        return drawn != nullptr ? drawn->matrix : current;
    }

    /// Keeps the matrices of this extraction for the next one.
    void Keep() {
        for (const auto& [entity, matrix] : m_drawn) {
            if (DrawnTransform* drawn = m_world->TryGet<DrawnTransform>(entity); drawn != nullptr) {
                drawn->matrix = matrix;
            } else {
                m_world->Add<DrawnTransform>(entity, matrix);
            }
        }
    }

private:
    ecs::World* m_world;
    std::vector<std::pair<ecs::Entity, glm::mat4>> m_drawn;
};

} // namespace

void ExtractScene(ecs::World& world, renderer::SceneView& view, ecs::Entity activeCamera) {
    DAI_PROFILE_SCOPE("scene::ExtractScene");
    view.camera.reset();
    view.draws.clear();
    view.jointMatrices.clear();
    view.previousJointMatrices.clear();
    view.lights.clear();
    view.fogVolumes.clear();
    DrawnTransforms drawn(world);

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
        [&](ecs::Entity entity, const ecs::WorldTransform& transform, const MeshRenderer& component) {
            view.draws.push_back({.mesh = component.mesh,
                                  .world = transform.matrix,
                                  .material = component.material,
                                  .castsShadows = component.castsShadows,
                                  .previousWorld = drawn.Previous(entity, transform.matrix),
                                  .id = SceneIdOf(entity)});
        });

    world.Each<ecs::WorldTransform, SkinnedMeshRenderer>(
        [&](ecs::Entity entity, const ecs::WorldTransform& transform, const SkinnedMeshRenderer& component) {
            const auto firstJoint = static_cast<std::uint32_t>(view.jointMatrices.size());
            const glm::mat4 previousWorld = drawn.Previous(entity, transform.matrix);
            const std::vector<glm::mat4>* inverseBind = component.inverseBindMatrices.get();
            for (std::size_t index = 0; index < component.joints.size(); ++index) {
                const ecs::WorldTransform* joint = world.TryGet<ecs::WorldTransform>(component.joints[index]);
                const glm::mat4 bind =
                    (inverseBind != nullptr && index < inverseBind->size()) ? (*inverseBind)[index] : glm::mat4(1.0f);
                if (joint != nullptr) {
                    view.jointMatrices.push_back(joint->matrix * bind);
                    view.previousJointMatrices.push_back(drawn.Previous(component.joints[index], joint->matrix) * bind);
                } else {
                    view.jointMatrices.push_back(transform.matrix * bind);
                    view.previousJointMatrices.push_back(previousWorld * bind);
                }
            }
            view.draws.push_back({.mesh = component.mesh,
                                  .world = transform.matrix,
                                  .material = component.material,
                                  .firstJoint = component.joints.empty() ? renderer::kNoJoints : firstJoint,
                                  .castsShadows = component.castsShadows,
                                  .previousWorld = previousWorld,
                                  .id = SceneIdOf(entity)});
        });
    drawn.Keep();
    ExtractLights(world, view);
    ExtractFogVolumes(world, view);
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
