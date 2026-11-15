// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/LightUnits.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
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
                                       .falloffHeight = volume.falloffHeight,
                                       .noise = volume.noise,
                                       .noiseFrequency = volume.noiseFrequency,
                                       .noiseVelocity = volume.noiseVelocity});
        });
}

void ExtractParticleForces(ecs::World& world, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, WindVolume>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const WindVolume& wind) {
            if (!wind.enabled) {
                return;
            }
            // The velocity turns with the entity, but its scale, the box's size, does not stretch it.
            const glm::mat3 axes(glm::normalize(glm::vec3(transform.matrix[0])),
                                 glm::normalize(glm::vec3(transform.matrix[1])),
                                 glm::normalize(glm::vec3(transform.matrix[2])));
            view.windVolumes.push_back({.world = transform.matrix,
                                        .velocity = axes * wind.velocity,
                                        .gust = wind.gust,
                                        .gustFrequency = wind.gustFrequency,
                                        .edge = wind.edge});
        });
    world.Each<ecs::WorldTransform, ParticleAttractor>(
        [&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform, const ParticleAttractor& attractor) {
            if (!attractor.enabled) {
                return;
            }
            view.particleAttractors.push_back({.position = glm::vec3(transform.matrix[3]),
                                               .axis = glm::normalize(glm::vec3(transform.matrix[1])),
                                               .effect = attractor.effect,
                                               .strength = attractor.strength,
                                               .vortex = attractor.vortex,
                                               .radius = attractor.radius});
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
    view.ribbons.clear();
    view.fogVolumes.clear();
    view.windVolumes.clear();
    view.particleAttractors.clear();
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
    ExtractParticleForces(world, view);
}

void EmitParticles(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns) {
    world.Each<ecs::WorldTransform, ParticleEmitter>([&](ecs::Entity /*entity*/, const ecs::WorldTransform& transform,
                                                         ParticleEmitter& emitter) {
        const float fade = emitter.emission.light.has_value() ? emitter.emission.light->fade : 0.0f;
        const auto dim = [&] { emitter.activity = fade > 0.0f ? emitter.activity * std::exp(-delta / fade) : 0.0f; };
        if (!emitter.enabled) {
            emitter.pending = 0.0f;
            emitter.age = 0.0f;
            emitter.lastPosition.reset();
            dim();
            return;
        }
        const glm::vec3 position(transform.matrix[3]);
        const glm::vec3 moved = emitter.lastPosition.has_value() ? position - *emitter.lastPosition : glm::vec3(0.0f);
        const ParticleEmission& emission = emitter.emission;
        emitter.pending +=
            std::max(emission.rate, 0.0f) * delta + std::max(emission.ratePerMeter, 0.0f) * glm::length(moved);
        const float whole = std::floor(emitter.pending);
        emitter.pending -= whole;
        std::uint32_t count = static_cast<std::uint32_t>(whole);
        for (const ParticleBurst& burst : emission.bursts) {
            count += BurstsBetween(burst, emitter.age, emitter.age + delta) * burst.count;
        }
        // Active while it emits steadily or has just emitted; its light fades once it stops.
        if (emission.rate > 0.0f || count > 0) {
            emitter.activity = 1.0f;
        } else {
            dim();
        }
        emitter.age += delta;
        const std::optional<glm::vec3> previous = std::exchange(emitter.lastPosition, position);
        if (count == 0) {
            return;
        }
        const glm::mat3 axes(glm::normalize(glm::vec3(transform.matrix[0])),
                             glm::normalize(glm::vec3(transform.matrix[1])),
                             glm::normalize(glm::vec3(transform.matrix[2])));
        spawns.push_back({.effect = emitter.effect,
                          .position = position,
                          .rotation = glm::quat_cast(axes),
                          .count = count,
                          .previousPosition = previous,
                          .velocity = previous.has_value() && delta > 0.0f ? moved / delta : glm::vec3(0.0f)});
    });
}

float Flicker(float rate, float depth, float time, std::uint32_t seed) {
    if (rate <= 0.0f || depth <= 0.0f) {
        return 1.0f;
    }
    // Three waves out of step with each other, and with other emitters': it never quite repeats.
    const float phase = 6.2831853f * rate * time;
    const auto offset = [seed](std::uint32_t salt) {
        return static_cast<float>(((seed + salt) * 2654435761U) >> 16U & 0xFFFFU) / 65535.0f * 6.2831853f;
    };
    const float wave = 0.5f * std::sin(phase + offset(1)) + 0.3f * std::sin(phase * 2.31f + offset(2)) +
                       0.2f * std::sin(phase * 4.73f + offset(3));
    return 1.0f - depth * (0.5f + 0.5f * wave);
}

namespace {

/// Adds an emitter's light at a position, as bright as its activity, flickering by its age; tag keeps its
/// name apart from the entity's other lights.
void AddEmitterLight(renderer::SceneView& view, ecs::Entity entity, const EmitterLight& light,
                     const glm::vec3& position, float activity, float age, std::uint64_t tag) {
    if (activity <= 0.001f) {
        return;
    }
    const float intensity =
        light.intensity * activity * Flicker(light.flickerRate, light.flickerDepth, age, entity.Index());
    if (intensity <= 0.0f || light.range <= 0.0f) {
        return;
    }
    view.lights.push_back(
        {.type = renderer::LightType::Point,
         .position = position,
         .color = light.color *
                  (light.temperature > 0.0f ? renderer::ColorTemperature(light.temperature) : glm::vec3(1.0f)) *
                  intensity,
         .range = light.range,
         .radius = light.radius,
         .id = SceneIdOf(entity) | tag});
}

// Names that keep emitters' lights apart from the entity's own.
constexpr std::uint64_t kParticleLightTag = std::uint64_t{1} << 63U;
constexpr std::uint64_t kRibbonLightTag = std::uint64_t{1} << 62U;

/// A value of keys at t, blended linearly, held before the first and after the last.
template <class Key, class Value>
Value Sample(const std::vector<Key>& keys, float t, Value Key::* member, Value fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (t <= keys.front().time) {
        return keys.front().*member;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const float span = keys[i].time - keys[i - 1].time;
            const float blend = span > 0.0f ? (t - keys[i - 1].time) / span : 1.0f;
            return keys[i - 1].*member + (keys[i].*member - keys[i - 1].*member) * blend;
        }
    }
    return keys.back().*member;
}

float Uniform(std::minstd_rand& random) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
}

/// The points between a and b after splitting the segment generations times, each middle moved aside in Y
/// and Z by up to amplitude, halved at each split; middles that start a branch are added to starts, with the
/// splits left after them.
std::vector<glm::vec3> Split(const glm::vec3& a, const glm::vec3& b, std::uint32_t generations, float amplitude,
                             float branches, std::minstd_rand& random,
                             std::vector<std::pair<std::size_t, std::uint32_t>>* starts) {
    std::vector<glm::vec3> points{a, b};
    for (std::uint32_t generation = 0; generation < generations; ++generation) {
        std::vector<glm::vec3> split;
        split.reserve(points.size() * 2 - 1);
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            split.push_back(points[i]);
            glm::vec3 middle = (points[i] + points[i + 1]) * 0.5f;
            middle.y += (Uniform(random) * 2.0f - 1.0f) * amplitude;
            middle.z += (Uniform(random) * 2.0f - 1.0f) * amplitude;
            split.push_back(middle);
        }
        split.push_back(points.back());
        // A branch starts at a middle of this split; its index is final once every split is done.
        if (starts != nullptr) {
            for (auto& [index, left] : *starts) {
                index *= 2;
            }
            for (std::size_t i = 1; i < split.size(); i += 2) {
                if (Uniform(random) < branches) {
                    starts->emplace_back(i, generations - generation - 1);
                }
            }
        }
        points = std::move(split);
        amplitude *= 0.5f;
    }
    return points;
}

/// The ribbon's points through positions, in order, with the color and width of each at its t along it.
renderer::Ribbon BuildRibbon(const RibbonEffect& effect, std::span<const glm::vec3> positions,
                             std::span<const float> along, float widthScale, float brightness, float scrolled) {
    renderer::Ribbon ribbon{.texture = effect.texture, .additive = effect.additive, .softness = effect.softness};
    ribbon.points.reserve(positions.size());
    float distance = 0.0f;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (i > 0) {
            distance += glm::distance(positions[i - 1], positions[i]);
        }
        const float t = along[i];
        glm::vec4 color = Sample(effect.color, t, &renderer::ParticleColorKey::color, glm::vec4(1.0f));
        color = glm::vec4(glm::vec3(color) * brightness, color.a);
        ribbon.points.push_back(
            {.position = positions[i],
             .width = Sample(effect.width, t, &renderer::ParticleCurveKey::value, 0.05f) * widthScale,
             .color = color,
             .u = distance / std::max(effect.tiling, 1.0e-3f) - scrolled});
    }
    return ribbon;
}

} // namespace

std::vector<std::vector<glm::vec3>> MakeBolt(std::uint32_t generations, float displacement, float branches,
                                             std::uint32_t seed) {
    std::minstd_rand random(seed + 1U);
    std::vector<std::pair<std::size_t, std::uint32_t>> starts;
    std::vector<std::vector<glm::vec3>> bolt;
    bolt.push_back(
        Split(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), generations, displacement, branches, random, &starts));
    const std::vector<glm::vec3>& main = bolt.front();
    for (const auto& [index, left] : starts) {
        if (index == 0 || index + 1 >= main.size()) {
            continue;
        }
        // On from the middle, turned aside, a third to two thirds of what is left of the bolt.
        const glm::vec3 from = main[index];
        glm::vec3 direction = main[index + 1] - main[index - 1];
        direction.y += (Uniform(random) * 2.0f - 1.0f) * glm::length(direction);
        direction.z += (Uniform(random) * 2.0f - 1.0f) * glm::length(direction);
        const float reach = (1.0f - from.x) * (0.33f + 0.33f * Uniform(random));
        if (reach <= 0.0f || glm::length(direction) <= 0.0f) {
            continue;
        }
        const glm::vec3 to = from + glm::normalize(direction) * reach;
        const float amplitude = displacement * reach;
        bolt.push_back(Split(from, to, std::max(left, 1U), amplitude, 0.0f, random, nullptr));
    }
    return bolt;
}

void ExtractRibbons(ecs::World& world, float delta, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, RibbonEmitter>([&](ecs::Entity entity, const ecs::WorldTransform& transform,
                                                       RibbonEmitter& emitter) {
        if (emitter.effect == nullptr) {
            return;
        }
        const RibbonEffect& effect = *emitter.effect;
        const float fade = effect.light.has_value() ? effect.light->fade : 0.0f;
        emitter.age += delta;
        if (!emitter.enabled) {
            emitter.activity = fade > 0.0f ? emitter.activity * std::exp(-delta / fade) : 0.0f;
            emitter.trail.clear();
            emitter.sinceShape = std::numeric_limits<float>::infinity();
        } else {
            emitter.activity = 1.0f;
        }
        const glm::vec3 start(transform.matrix[3]);
        glm::vec3 end = start + glm::normalize(glm::vec3(transform.matrix[1])) * effect.length;
        if (world.IsAlive(emitter.target)) {
            if (const ecs::WorldTransform* target = world.TryGet<ecs::WorldTransform>(emitter.target)) {
                end = glm::vec3(target->matrix[3]);
            }
        }
        const float scrolled = emitter.age * effect.scroll;
        glm::vec3 lightAt = (start + end) * 0.5f;

        if (emitter.enabled) {
            if (effect.shape == RibbonShape::Trail) {
                // The newest point follows the emitter; another is left behind every spacing meters.
                for (RibbonEmitter::TrailPoint& point : emitter.trail) {
                    point.age += delta;
                }
                while (!emitter.trail.empty() && emitter.trail.back().age > effect.duration) {
                    emitter.trail.pop_back();
                }
                if (emitter.trail.size() < 2 ||
                    glm::distance(emitter.trail[1].position, start) >= std::max(effect.spacing, 1.0e-3f)) {
                    emitter.trail.push_front({start, 0.0f});
                } else {
                    emitter.trail.front() = {start, 0.0f};
                }
                lightAt = start;
                if (emitter.trail.size() >= 2) {
                    std::vector<glm::vec3> positions;
                    std::vector<float> along;
                    for (const RibbonEmitter::TrailPoint& point : emitter.trail) {
                        positions.push_back(point.position);
                        along.push_back(effect.duration > 0.0f ? std::min(point.age / effect.duration, 1.0f) : 0.0f);
                    }
                    view.ribbons.push_back(BuildRibbon(effect, positions, along, 1.0f, 1.0f, scrolled));
                }
            } else {
                const glm::vec3 line = end - start;
                const float length = glm::length(line);
                if (length > 1.0e-4f) {
                    // A frame across the line, in which a bolt's points are given.
                    const glm::vec3 forward = line / length;
                    const glm::vec3 helper =
                        std::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 side = glm::normalize(glm::cross(forward, helper));
                    const glm::vec3 up = glm::cross(side, forward);
                    const auto place = [&](const glm::vec3& p) {
                        return start + line * p.x + (side * p.y + up * p.z) * length;
                    };
                    std::vector<std::vector<glm::vec3>> shapes;
                    float brightness = 1.0f;
                    if (effect.shape == RibbonShape::Bolt) {
                        emitter.sinceShape += delta;
                        if (emitter.bolt.empty() || emitter.sinceShape >= effect.refresh) {
                            const std::uint32_t seed = entity.Index() * 7919U + emitter.shapes++;
                            emitter.bolt = MakeBolt(effect.generations, effect.displacement, effect.branches, seed);
                            std::minstd_rand random(seed);
                            emitter.brightness = 1.0f - effect.flicker * Uniform(random);
                            emitter.sinceShape = 0.0f;
                        }
                        shapes = emitter.bolt;
                        brightness = emitter.brightness;
                    } else {
                        // Enough points for the color and width to follow their keys.
                        const std::size_t segments =
                            std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(length / 0.25f)), 1, 32);
                        std::vector<glm::vec3>& points = shapes.emplace_back();
                        for (std::size_t i = 0; i <= segments; ++i) {
                            points.emplace_back(static_cast<float>(i) / static_cast<float>(segments), 0.0f, 0.0f);
                        }
                    }
                    for (std::size_t i = 0; i < shapes.size(); ++i) {
                        std::vector<glm::vec3> positions;
                        std::vector<float> along;
                        for (const glm::vec3& p : shapes[i]) {
                            positions.push_back(place(p));
                            along.push_back(std::clamp(p.x, 0.0f, 1.0f));
                        }
                        // Branches are thinner.
                        view.ribbons.push_back(
                            BuildRibbon(effect, positions, along, i == 0 ? 1.0f : 0.6f, brightness, scrolled));
                    }
                    lightAt = place(glm::vec3(0.5f, 0.0f, 0.0f));
                    if (effect.light.has_value()) {
                        emitter.activity = brightness;
                    }
                }
            }
        }
        if (effect.light.has_value()) {
            AddEmitterLight(view, entity, *effect.light, lightAt + effect.light->offset, emitter.activity, emitter.age,
                            kRibbonLightTag);
        }
    });
}

void ExtractEmitterLights(ecs::World& world, renderer::SceneView& view) {
    world.Each<ecs::WorldTransform, ParticleEmitter>(
        [&](ecs::Entity entity, const ecs::WorldTransform& transform, const ParticleEmitter& emitter) {
            if (!emitter.emission.light.has_value()) {
                return;
            }
            const EmitterLight& light = *emitter.emission.light;
            // Its own name, apart from a light the entity may have of its own.
            AddEmitterLight(view, entity, light, glm::vec3(transform.matrix * glm::vec4(light.offset, 1.0f)),
                            emitter.activity, emitter.age, kParticleLightTag);
        });
}

std::uint32_t BurstsBetween(const ParticleBurst& burst, float from, float to) {
    if (to <= from) {
        return 0;
    }
    if (burst.interval <= 0.0f) {
        return burst.time >= from && burst.time < to ? 1U : 0U;
    }
    // The firings at time + k * interval, k from 0, that fall in [from, to).
    const double first = std::max(0.0, std::ceil((static_cast<double>(from) - burst.time) / burst.interval));
    double last = std::ceil((static_cast<double>(to) - burst.time) / burst.interval) - 1.0;
    if (burst.repeats > 0) {
        last = std::min(last, static_cast<double>(burst.repeats) - 1.0);
    }
    return last >= first ? static_cast<std::uint32_t>(last - first + 1.0) : 0U;
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
