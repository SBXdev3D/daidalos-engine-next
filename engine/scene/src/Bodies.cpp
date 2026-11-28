// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/math/Transform.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Bodies.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace daidalos::scene {

namespace {

constexpr std::array<std::pair<physics::ShapeKind, std::string_view>, 5> kShapeNames{{
    {physics::ShapeKind::Box, "box"},
    {physics::ShapeKind::Sphere, "sphere"},
    {physics::ShapeKind::Cylinder, "cylinder"},
    {physics::ShapeKind::Capsule, "capsule"},
    {physics::ShapeKind::Hull, "hull"},
}};

// Hits: no sound below a gentle touch, full volume from a hard knock; one hit per body in this many seconds,
// so that a box settling on its corners does not rattle; at most this many hits in a step.
constexpr float kQuietestHit = 0.35f;
constexpr float kLoudestHit = 5.0f;
constexpr float kHitPause = 0.12f;
constexpr int kHitsPerStep = 6;
constexpr float kHeavyMomentum = 30.0f;
// Dust from landings at least this fast and this heavy.
constexpr float kDustSpeed = 2.5f;
constexpr float kDustMomentum = 60.0f;
// Rolling and scraping: from this speed, louder up to the second; how fast the loops follow.
constexpr float kLoopSpeed = 0.25f;
constexpr float kRollLoudest = 4.0f;
constexpr float kScrapeLoudest = 3.0f;
constexpr float kLoopResponse = 10.0f;

/// The bank's clips for a surface, or wood's, or any.
template <class Clips>
const typename Clips::mapped_type* Find(const Clips& clips, std::string_view surface) {
    auto found = clips.find(surface);
    if (found == clips.end()) {
        found = clips.find("wood");
    }
    if (found == clips.end()) {
        found = clips.begin();
    }
    return found == clips.end() ? nullptr : &found->second;
}

} // namespace

std::string_view BodyShapeName(physics::ShapeKind shape) {
    for (const auto& [kind, name] : kShapeNames) {
        if (kind == shape) {
            return name;
        }
    }
    return "box";
}

std::optional<physics::ShapeKind> BodyShapeOf(std::string_view name) {
    for (const auto& [kind, known] : kShapeNames) {
        if (known == name) {
            return kind;
        }
    }
    return std::nullopt;
}

physics::BodyMaterial SurfaceMaterial(std::string_view surface) {
    // Friction, restitution, linear and angular damping.
    if (surface == "metal") {
        return {0.45f, 0.15f, 0.05f, 0.05f};
    }
    if (surface == "plastic") {
        return {0.5f, 0.3f, 0.05f, 0.05f};
    }
    if (surface == "cloth") {
        return {0.9f, 0.0f, 0.1f, 0.4f};
    }
    if (surface == "glass") {
        return {0.4f, 0.1f, 0.05f, 0.05f};
    }
    if (surface == "rubber") {
        return {0.95f, 0.75f, 0.05f, 0.1f};
    }
    if (surface == "concrete") {
        return {0.75f, 0.05f, 0.05f, 0.05f};
    }
    if (surface == "tile") {
        return {0.5f, 0.05f, 0.05f, 0.05f};
    }
    if (surface == "ice") {
        return {0.03f, 0.05f, 0.0f, 0.05f};
    }
    return {0.6f, 0.15f, 0.05f, 0.05f}; // Wood, and what is not known.
}

std::string MainSurface(const Model& model) {
    std::map<std::string, std::size_t, std::less<>> triangles;
    for (const Model::Node& node : model.nodes) {
        for (const std::uint32_t index : node.parts) {
            const Model::Part& part = model.parts[index];
            if (!part.surface.empty()) {
                triangles[part.surface] += part.indices.size() / 3;
            }
        }
    }
    std::string main;
    std::size_t most = 0;
    for (const auto& [surface, count] : triangles) {
        const bool glass = surface == "glass";
        // Glass only wins when nothing else is there.
        if (main.empty() || (!glass && (main == "glass" || count > most))) {
            main = surface;
            most = count;
        }
    }
    return main;
}

std::vector<glm::vec3> ModelPoints(const Model& model) {
    std::vector<glm::mat4> placed(model.nodes.size(), glm::mat4(1.0f));
    std::vector<glm::vec3> points;
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const Model::Node& node = model.nodes[index];
        placed[index] = math::ToMatrix(node.local);
        if (node.parent.has_value()) {
            placed[index] = placed[*node.parent] * placed[index];
        }
        if (node.skin.has_value()) {
            continue;
        }
        for (const std::uint32_t part : node.parts) {
            for (const glm::vec3& position : model.parts[part].positions) {
                points.push_back(math::TransformPoint(placed[index], position));
            }
        }
    }
    return points;
}

float HitVolume(float approach) {
    return std::clamp((approach - kQuietestHit) / (kLoudestHit - kQuietestHit), 0.0f, 1.0f);
}

bool IsHeavyHit(float approach, float mass) {
    return approach * mass >= kHeavyMomentum;
}

float HitPitch(float mass) {
    return std::clamp(std::pow(10.0f / std::max(mass, 0.1f), 0.12f), 0.75f, 1.3f);
}

void PhysicsFeedback::Update(ecs::World& world, const physics::PhysicsWorld& physics, audio::SoundWorld& sounds,
                             const SpawnDust& spawnDust, float step) {
    m_clock += step;
    struct Known {
        std::string_view surface;
        float mass = 0.0f;
        float slide = 0.0f;
        bool touching = false;
    };
    std::unordered_map<std::uint32_t, Known> bodies;
    world.Each<RigidBody>([&](ecs::Entity, const RigidBody& body) {
        bodies.emplace(body.body.value, Known{.surface = body.surface, .mass = physics.BodyMass(body.body)});
    });

    int hits = 0;
    for (const physics::Contact& contact : physics.Contacts()) {
        Known* first = nullptr;
        Known* second = nullptr;
        if (auto found = bodies.find(contact.first.value); found != bodies.end()) {
            first = &found->second;
        }
        if (auto found = bodies.find(contact.second.value); found != bodies.end()) {
            second = &found->second;
        }
        for (Known* side : {first, second}) {
            if (side != nullptr) {
                side->touching = true;
                side->slide = std::max(side->slide, contact.slide);
            }
        }
        if (!contact.fresh || (first == nullptr && second == nullptr) || hits >= kHitsPerStep) {
            continue;
        }
        // The heavier of the two loose objects makes the sound.
        const bool firstSounds = first != nullptr && (second == nullptr || first->mass >= second->mass);
        const Known& sounding = firstSounds ? *first : *second;
        const std::uint32_t id = firstSounds ? contact.first.value : contact.second.value;
        if (HitVolume(contact.approach) <= 0.0f) {
            continue;
        }
        if (const auto last = m_lastHit.find(id); last != m_lastHit.end() && m_clock - last->second < kHitPause) {
            continue;
        }
        m_lastHit[id] = m_clock;
        ++hits;
        Hit(sounds, sounding.surface, contact.point, contact.approach, sounding.mass);
        if (spawnDust && !m_sounds.dust.IsNull() && contact.approach >= kDustSpeed &&
            contact.approach * sounding.mass >= kDustMomentum) {
            const float momentum = contact.approach * sounding.mass;
            const auto count = static_cast<std::uint32_t>(std::clamp(momentum / 30.0f, 2.0f, 10.0f));
            const glm::vec3 up = contact.normal.y >= 0.0f ? contact.normal : -contact.normal;
            spawnDust(contact.point, up, count);
        }
    }

    // Joints that broke (ADR-0126) snap, as the body they held is made.
    if (!physics.BrokenJoints().empty()) {
        std::unordered_map<std::uint32_t, std::string_view> held;
        world.Each<Joints, RigidBody>([&held](ecs::Entity, const Joints& joints, const RigidBody& body) {
            for (const physics::JointHandle joint : joints.handles) {
                held.emplace(joint.Index(), body.surface);
            }
        });
        for (const physics::JointBreak& snap : physics.BrokenJoints()) {
            const auto found = held.find(snap.joint.Index());
            const audio::ClipHandle* clip =
                Find(m_sounds.snaps, found != held.end() ? found->second : std::string_view{});
            if (clip != nullptr) {
                sounds.Play(*clip,
                            {.position = snap.position, .volume = 0.9f, .minDistance = 1.5f, .maxDistance = 35.0f});
            }
            ++m_snaps;
        }
    }

    // Rolling and scraping loops, which follow how fast each body rolls or slides along what it touches.
    const float follow = std::min(step * kLoopResponse, 1.0f);
    for (auto& [id, known] : bodies) {
        float roll = 0.0f;
        float scrape = 0.0f;
        if (known.touching) {
            const float speed = glm::length(physics.BodyVelocity(physics::BodyId{id}));
            const float weight = std::clamp(known.mass / 20.0f, 0.3f, 1.0f);
            if (speed > kLoopSpeed && known.slide < 0.35f * speed) {
                roll = std::clamp(speed / kRollLoudest, 0.0f, 1.0f) * weight;
            }
            if (known.slide > kLoopSpeed) {
                scrape = std::clamp(known.slide / kScrapeLoudest, 0.0f, 1.0f) * weight;
            }
        }
        auto found = m_loops.find(id);
        if (found == m_loops.end()) {
            if (roll <= 0.0f && scrape <= 0.0f) {
                continue;
            }
            found = m_loops.emplace(id, Loops{}).first;
        }
        Loops& loops = found->second;
        loops.rollLevel += (roll - loops.rollLevel) * follow;
        loops.scrapeLevel += (scrape - loops.scrapeLevel) * follow;
        const glm::vec3 position = physics.BodyPosition(physics::BodyId{id});
        const audio::ClipHandle* rollClip = Find(m_sounds.rolls, known.surface);
        const audio::ClipHandle* scrapeClip = Find(m_sounds.scrapes, known.surface);
        Loop(sounds, loops.roll, rollClip != nullptr ? *rollClip : audio::ClipHandle{}, loops.rollLevel, position);
        Loop(sounds, loops.scrape, scrapeClip != nullptr ? *scrapeClip : audio::ClipHandle{}, loops.scrapeLevel,
             position);
    }
    // Loops of bodies that are gone, or quiet again.
    for (auto loop = m_loops.begin(); loop != m_loops.end();) {
        Loops& loops = loop->second;
        if (!bodies.contains(loop->first)) {
            sounds.Stop(loops.roll);
            sounds.Stop(loops.scrape);
            loops.roll = {};
            loops.scrape = {};
        }
        if (loops.roll.IsNull() && loops.scrape.IsNull()) {
            loop = m_loops.erase(loop);
        } else {
            ++loop;
        }
    }
    std::erase_if(m_lastHit, [this](const auto& last) { return m_clock - last.second > 1.0f; });
}

void PhysicsFeedback::Hit(audio::SoundWorld& sounds, std::string_view surface, const glm::vec3& point, float approach,
                          float mass) {
    const auto* clips = Find(IsHeavyHit(approach, mass) ? m_sounds.heavyHits : m_sounds.lightHits, surface);
    if (clips == nullptr || clips->empty()) {
        clips = Find(m_sounds.lightHits, surface);
    }
    if (clips == nullptr || clips->empty()) {
        return;
    }
    const audio::ClipHandle clip = (*clips)[m_random() % clips->size()];
    const float wobble = 0.94f + 0.12f * static_cast<float>(m_random() % 1000) / 1000.0f;
    const float volume = HitVolume(approach);
    sounds.Play(clip, {.position = point,
                       .volume = 0.25f + 0.75f * volume,
                       .minDistance = 1.0f,
                       .maxDistance = 30.0f,
                       .pitch = HitPitch(mass) * wobble});
    ++m_hits;
}

void PhysicsFeedback::Loop(audio::SoundWorld& sounds, audio::VoiceHandle& voice, audio::ClipHandle clip, float level,
                           const glm::vec3& position) {
    if (level < 0.01f || clip.IsNull()) {
        if (!voice.IsNull()) {
            sounds.Stop(voice);
            voice = {};
        }
        return;
    }
    if (voice.IsNull()) {
        voice = sounds.Play(
            clip, {.position = position, .volume = level, .loop = true, .minDistance = 1.0f, .maxDistance = 25.0f});
        return;
    }
    sounds.SetVolume(voice, level);
    sounds.SetPosition(voice, position);
}

void PhysicsFeedback::Stop(audio::SoundWorld& sounds) {
    for (auto& [id, loops] : m_loops) {
        sounds.Stop(loops.roll);
        sounds.Stop(loops.scrape);
    }
    m_loops.clear();
}

} // namespace daidalos::scene
