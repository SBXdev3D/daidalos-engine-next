// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>

// clang-format off
// Jolt.h must come first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <mutex>
#include <utility>
#include <vector>

namespace daidalos::physics {

namespace {

constexpr const char* kModule = "physics";

constexpr std::uint32_t kMaxBodies = 4096;
constexpr std::uint32_t kMaxBodyPairs = 4096;
constexpr std::uint32_t kMaxContacts = 2048;
constexpr std::size_t kTempMemory = 16 * 1024 * 1024;

// What never moves does not collide with itself. The bodies inside characters collide with nothing:
// characters push each other through their own test, and the bodies are only there for rays.
constexpr JPH::ObjectLayer kStaticLayer = 0;
constexpr JPH::ObjectLayer kMovingLayer = 1;
constexpr JPH::ObjectLayer kCharacterLayer = 2;
constexpr JPH::BroadPhaseLayer kStaticBroadPhase{0};
constexpr JPH::BroadPhaseLayer kMovingBroadPhase{1};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == kStaticLayer ? kStaticBroadPhase : kMovingBroadPhase;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == kStaticBroadPhase ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override {
        return object == kMovingLayer || broadPhase == kMovingBroadPhase;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        if (first == kCharacterLayer || second == kCharacterLayer) {
            return false;
        }
        return first == kMovingLayer || second == kMovingLayer;
    }
};

void Trace(const char* format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    DAI_LOG_DEBUG(kModule, "{}", message);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssert(const char* expression, const char* message, const char* file, JPH::uint line) {
    DAI_LOG_ERROR(kModule, "Jolt assertion {} at {}:{}: {}", expression, file, line, message ? message : "");
    return true; // Breaks into the debugger.
}
#endif

// Jolt keeps process-wide state, shared by all worlds.
int g_worlds = 0;

void AcquireJolt() {
    if (g_worlds++ > 0) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Trace = Trace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssert;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void ReleaseJolt() {
    if (--g_worlds > 0) {
        return;
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

JPH::Vec3 ToJolt(const glm::vec3& value) {
    return JPH::Vec3(value.x, value.y, value.z);
}

JPH::Quat ToJolt(const glm::quat& value) {
    return JPH::Quat(value.x, value.y, value.z, value.w);
}

glm::vec3 ToGlm(JPH::Vec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

// A hull keeps the points that stick out furthest along these many directions, spread over the sphere, so that a
// model of thousands of vertices makes a hull Jolt builds quickly.
constexpr std::size_t kHullDirections = 64;

std::vector<glm::vec3> HullPoints(std::span<const glm::vec3> points) {
    if (points.size() <= kHullDirections) {
        return {points.begin(), points.end()};
    }
    std::vector<std::size_t> kept;
    kept.reserve(kHullDirections);
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (std::size_t index = 0; index < kHullDirections; ++index) {
        const float y = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / static_cast<float>(kHullDirections);
        const float across = std::sqrt(std::max(1.0f - y * y, 0.0f));
        const float angle = golden * static_cast<float>(index);
        const glm::vec3 direction(std::cos(angle) * across, y, std::sin(angle) * across);
        std::size_t best = 0;
        float furthest = glm::dot(points[0], direction);
        for (std::size_t point = 1; point < points.size(); ++point) {
            const float along = glm::dot(points[point], direction);
            if (along > furthest) {
                furthest = along;
                best = point;
            }
        }
        kept.push_back(best);
    }
    std::ranges::sort(kept);
    const auto duplicates = std::ranges::unique(kept);
    kept.erase(duplicates.begin(), duplicates.end());
    std::vector<glm::vec3> hull;
    hull.reserve(kept.size());
    for (const std::size_t index : kept) {
        hull.push_back(points[index]);
    }
    return hull;
}

core::Result<JPH::RefConst<JPH::Shape>> MakeShape(const BodyShape& desc) {
    const glm::vec3 size = glm::max(desc.size, glm::vec3(0.02f));
    JPH::RefConst<JPH::Shape> shape;
    switch (desc.kind) {
    case ShapeKind::Box: {
        const glm::vec3 half = size * 0.5f;
        shape = new JPH::BoxShape(ToJolt(half), std::min({JPH::cDefaultConvexRadius, half.x, half.y, half.z}));
        break;
    }
    case ShapeKind::Sphere:
        shape = new JPH::SphereShape(std::min({size.x, size.y, size.z}) * 0.5f);
        break;
    case ShapeKind::Cylinder: {
        const float radius = std::min(size.x, size.z) * 0.5f;
        const float halfHeight = size.y * 0.5f;
        shape = new JPH::CylinderShape(halfHeight, radius, std::min({JPH::cDefaultConvexRadius, radius, halfHeight}));
        break;
    }
    case ShapeKind::Capsule: {
        const float radius = std::min({size.x, size.y, size.z}) * 0.5f;
        shape = new JPH::CapsuleShape(std::max(size.y * 0.5f - radius, 0.005f), radius);
        break;
    }
    case ShapeKind::Hull: {
        JPH::Array<JPH::Vec3> points;
        for (const glm::vec3& point : HullPoints(desc.points)) {
            points.push_back(ToJolt(point));
        }
        const JPH::ShapeSettings::ShapeResult hull = JPH::ConvexHullShapeSettings(points).Create();
        if (hull.HasError()) {
            return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule,
                                              std::format("Hull cannot collide: {}", hull.GetError().c_str())});
        }
        // Its points are already where they are in the body's frame.
        return JPH::RefConst<JPH::Shape>(hull.Get());
    }
    }
    if (desc.center == glm::vec3(0.0f)) {
        return shape;
    }
    const JPH::ShapeSettings::ShapeResult moved =
        JPH::RotatedTranslatedShapeSettings(ToJolt(desc.center), JPH::Quat::sIdentity(), shape).Create();
    if (moved.HasError()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule,
                                          std::format("Shape cannot collide: {}", moved.GetError().c_str())});
    }
    return JPH::RefConst<JPH::Shape>(moved.Get());
}

// Below this, a touch that goes on is not worth reporting: neither body is moving.
constexpr float kRestingSpeed = 0.05f;

/// Collects what touched during a step. Jolt may call it from its jobs, so it locks, although the world steps on
/// one thread today.
class ContactCollector final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& /*settings*/) override {
        Add(first, second, manifold, true);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& /*settings*/) override {
        const auto moving = [](const JPH::Body& body) {
            return body.IsDynamic() && body.GetLinearVelocity().LengthSq() > kRestingSpeed * kRestingSpeed;
        };
        if (moving(first) || moving(second)) {
            Add(first, second, manifold, false);
        }
    }

    void Clear() {
        const std::scoped_lock lock(m_mutex);
        m_contacts.clear();
    }

    [[nodiscard]] std::span<const Contact> Contacts() const { return m_contacts; }

private:
    void Add(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold, bool fresh) {
        if (manifold.mRelativeContactPointsOn1.empty()) {
            return;
        }
        // The middle of the points where they touch.
        JPH::Vec3 sum = JPH::Vec3::sZero();
        for (JPH::uint index = 0; index < manifold.mRelativeContactPointsOn1.size(); ++index) {
            sum += JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(index));
        }
        const JPH::RVec3 point(sum / static_cast<float>(manifold.mRelativeContactPointsOn1.size()));
        // Jolt's normal points from the first body to the second.
        const JPH::Vec3 normal = manifold.mWorldSpaceNormal;
        const JPH::Vec3 relative = first.GetPointVelocity(point) - second.GetPointVelocity(point);
        const float along = relative.Dot(normal);
        Contact contact;
        contact.first = BodyId{first.GetID().GetIndexAndSequenceNumber()};
        contact.second = BodyId{second.GetID().GetIndexAndSequenceNumber()};
        contact.point = ToGlm(JPH::Vec3(point));
        contact.normal = -ToGlm(normal);
        contact.approach = fresh ? std::max(along, 0.0f) : 0.0f;
        contact.slide = (relative - normal * along).Length();
        contact.fresh = fresh;
        const std::scoped_lock lock(m_mutex);
        m_contacts.push_back(contact);
    }

    std::mutex m_mutex;
    std::vector<Contact> m_contacts;
};

} // namespace

struct PhysicsWorld::State {
    struct Character {
        CharacterConfig config;
        JPH::Ref<JPH::CharacterVirtual> controller;
        /// How fast the last move really went. Jolt only remembers the velocity it was asked for.
        glm::vec3 velocity{0.0f};
    };

    State() : temp(kTempMemory), jobs(JPH::cMaxPhysicsJobs) {
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContacts, layers, objectVsBroadPhase, objectPairs);
        // Loose objects fall at the Earth's gravity (ADR-0073); characters are virtual, and the game applies its
        // own gravity to them.
        system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
        system.SetContactListener(&contacts);
    }

    [[nodiscard]] Character& Find(CharacterHandle character) { return characters.Get(character); }

    [[nodiscard]] static JPH::Ref<JPH::Shape> MakeCharacterShape(const CharacterConfig& config, float height) {
        // The origin of the shape is at the feet.
        const float halfCylinder = std::max(height * 0.5f - config.radius, 0.01f);
        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(halfCylinder, config.radius);
        return JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, halfCylinder + config.radius, 0.0f),
                                                   JPH::Quat::sIdentity(), capsule)
            .Create()
            .Get();
    }

    BroadPhaseLayers layers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    JPH::TempAllocatorImpl temp;
    JPH::JobSystemSingleThreaded jobs;
    JPH::PhysicsSystem system;
    JPH::CharacterVsCharacterCollisionSimple characterCollisions;
    core::SlotPool<Character, CharacterTag> characters;
    ContactCollector contacts;
};

PhysicsWorld::PhysicsWorld() {
    AcquireJolt();
    m_state = std::make_unique<State>();
}

PhysicsWorld::~PhysicsWorld() {
    if (!m_state) {
        return;
    }
    m_state->characters = {}; // Before the bodies: each character has one.
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    JPH::BodyIDVector all;
    m_state->system.GetBodies(all);
    for (const JPH::BodyID id : all) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    m_state.reset();
    ReleaseJolt();
}

core::Result<BodyId> PhysicsWorld::AddStaticMesh(std::span<const glm::vec3> positions,
                                                 std::span<const std::uint32_t> indices,
                                                 const math::Transform& transform, const BodyMaterial& material) {
    DAI_PROFILE_SCOPE("physics::AddStaticMesh");
    JPH::VertexList vertices;
    vertices.reserve(positions.size());
    for (const glm::vec3& position : positions) {
        vertices.push_back(JPH::Float3(position.x, position.y, position.z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        triangles.push_back(JPH::IndexedTriangle(indices[index], indices[index + 1], indices[index + 2]));
    }
    const JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    const JPH::ShapeSettings::ShapeResult shape = settings.Create();
    if (shape.HasError()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule,
                                          std::format("Mesh cannot collide: {}", shape.GetError().c_str())});
    }
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(ToJolt(transform.translation)), ToJolt(transform.rotation),
                                   JPH::EMotionType::Static, kStaticLayer);
    body.mFriction = std::max(material.friction, 0.0f);
    body.mRestitution = std::clamp(material.restitution, 0.0f, 1.0f);
    const JPH::BodyID id = m_state->system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule, "Too many bodies"});
    }
    return BodyId{id.GetIndexAndSequenceNumber()};
}

core::Result<BodyId> PhysicsWorld::AddKinematicBox(const glm::vec3& halfExtents, const glm::vec3& center,
                                                   const math::Transform& transform) {
    const float convexRadius = std::min({JPH::cDefaultConvexRadius, halfExtents.x, halfExtents.y, halfExtents.z});
    JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(ToJolt(halfExtents), convexRadius);
    const JPH::ShapeSettings::ShapeResult shape =
        JPH::RotatedTranslatedShapeSettings(ToJolt(center), JPH::Quat::sIdentity(), box).Create();
    if (shape.HasError()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule,
                                          std::format("Box cannot collide: {}", shape.GetError().c_str())});
    }
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(ToJolt(transform.translation)), ToJolt(transform.rotation),
                                   JPH::EMotionType::Kinematic, kMovingLayer);
    body.mAllowSleeping = false;
    const JPH::BodyID id = m_state->system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule, "Too many bodies"});
    }
    return BodyId{id.GetIndexAndSequenceNumber()};
}

core::Result<BodyId> PhysicsWorld::AddDynamicBox(const glm::vec3& halfExtents, float mass,
                                                 const math::Transform& transform, const glm::vec3& center) {
    DynamicBodyDesc desc;
    desc.shape.size = halfExtents * 2.0f;
    desc.shape.center = center;
    desc.mass = mass;
    return AddDynamicBody(desc, transform);
}

core::Result<BodyId> PhysicsWorld::AddDynamicBody(const DynamicBodyDesc& desc, const math::Transform& transform) {
    DAI_TRY_ASSIGN(const JPH::RefConst<JPH::Shape> shape, MakeShape(desc.shape));
    JPH::BodyCreationSettings body(shape, JPH::RVec3(ToJolt(transform.translation)), ToJolt(transform.rotation),
                                   JPH::EMotionType::Dynamic, kMovingLayer);
    body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.01f);
    body.mFriction = std::max(desc.material.friction, 0.0f);
    body.mRestitution = std::clamp(desc.material.restitution, 0.0f, 1.0f);
    body.mLinearDamping = std::max(desc.material.linearDamping, 0.0f);
    body.mAngularDamping = std::max(desc.material.angularDamping, 0.0f);
    body.mMotionQuality = desc.fast ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    const JPH::BodyID id = m_state->system.GetBodyInterface().CreateAndAddBody(body, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule, "Too many bodies"});
    }
    return BodyId{id.GetIndexAndSequenceNumber()};
}

glm::quat PhysicsWorld::BodyRotation(BodyId body) const {
    const JPH::Quat rotation = m_state->system.GetBodyInterface().GetRotation(JPH::BodyID(body.value));
    return glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());
}

glm::vec3 PhysicsWorld::BodyVelocity(BodyId body) const {
    return ToGlm(m_state->system.GetBodyInterface().GetLinearVelocity(JPH::BodyID(body.value)));
}

glm::vec3 PhysicsWorld::BodyAngularVelocity(BodyId body) const {
    return ToGlm(m_state->system.GetBodyInterface().GetAngularVelocity(JPH::BodyID(body.value)));
}

void PhysicsWorld::SetBodyVelocity(BodyId body, const glm::vec3& linear, const glm::vec3& angular) {
    m_state->system.GetBodyInterface().SetLinearAndAngularVelocity(JPH::BodyID(body.value), ToJolt(linear),
                                                                   ToJolt(angular));
    m_state->system.GetBodyInterface().ActivateBody(JPH::BodyID(body.value));
}

void PhysicsWorld::SetBodyTransform(BodyId body, const glm::vec3& position, const glm::quat& rotation) {
    m_state->system.GetBodyInterface().SetPositionAndRotation(JPH::BodyID(body.value), JPH::RVec3(ToJolt(position)),
                                                              ToJolt(rotation), JPH::EActivation::Activate);
}

void PhysicsWorld::AddImpulse(BodyId body, const glm::vec3& impulse) {
    m_state->system.GetBodyInterface().AddImpulse(JPH::BodyID(body.value), ToJolt(impulse));
}

void PhysicsWorld::AddImpulseAt(BodyId body, const glm::vec3& impulse, const glm::vec3& point) {
    m_state->system.GetBodyInterface().AddImpulse(JPH::BodyID(body.value), ToJolt(impulse), JPH::RVec3(ToJolt(point)));
}

bool PhysicsWorld::IsSleeping(BodyId body) const {
    const JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID id(body.value);
    return bodies.GetMotionType(id) == JPH::EMotionType::Dynamic && !bodies.IsActive(id);
}

std::vector<BodyId> PhysicsWorld::DynamicBodiesInSphere(const glm::vec3& center, float radius) const {
    std::vector<BodyId> found;
    if (radius <= 0.0f) {
        return found;
    }
    const JPH::RefConst<JPH::Shape> sphere = new JPH::SphereShape(radius);
    const JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    const JPH::NarrowPhaseQuery& query = m_state->system.GetNarrowPhaseQuery();
    const JPH::DefaultBroadPhaseLayerFilter broadPhase(m_state->objectVsBroadPhase, kMovingLayer);
    const JPH::SpecifiedObjectLayerFilter moving(kMovingLayer);
    query.CollideShape(sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(JPH::RVec3(ToJolt(center))),
                       settings, JPH::RVec3::sZero(), hits, broadPhase, moving);
    const JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    for (const JPH::CollideShapeResult& hit : hits.mHits) {
        const BodyId body{hit.mBodyID2.GetIndexAndSequenceNumber()};
        if (bodies.GetMotionType(hit.mBodyID2) == JPH::EMotionType::Dynamic &&
            std::ranges::find(found, body) == found.end()) {
            found.push_back(body);
        }
    }
    return found;
}

bool PhysicsWorld::IsDynamic(BodyId body) const {
    return m_state->system.GetBodyInterface().GetMotionType(JPH::BodyID(body.value)) == JPH::EMotionType::Dynamic;
}

float PhysicsWorld::BodyMass(BodyId body) const {
    const JPH::BodyLockRead lock(m_state->system.GetBodyLockInterface(), JPH::BodyID(body.value));
    if (!lock.Succeeded() || lock.GetBody().GetMotionProperties() == nullptr) {
        return 0.0f;
    }
    return 1.0f / std::max(lock.GetBody().GetMotionProperties()->GetInverseMass(), 1e-6f);
}

void PhysicsWorld::RemoveBody(BodyId body) {
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID id(body.value);
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}

std::size_t PhysicsWorld::BodyCount() const {
    // The bodies inside characters are not counted: they come and go with their characters.
    return m_state->system.GetNumBodies() - m_state->characters.Size();
}

void PhysicsWorld::MoveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float step) {
    m_state->system.GetBodyInterface().MoveKinematic(JPH::BodyID(body.value), JPH::RVec3(ToJolt(position)),
                                                     ToJolt(rotation), step);
}

glm::vec3 PhysicsWorld::BodyPosition(BodyId body) const {
    return ToGlm(JPH::Vec3(m_state->system.GetBodyInterface().GetPosition(JPH::BodyID(body.value))));
}

void PhysicsWorld::Step(float step) {
    DAI_PROFILE_SCOPE("physics::Step");
    m_state->contacts.Clear();
    m_state->system.Update(step, 1, &m_state->temp, &m_state->jobs);
}

std::span<const Contact> PhysicsWorld::Contacts() const {
    return m_state->contacts.Contacts();
}

std::vector<BodyOutline> PhysicsWorld::Outlines(const glm::vec3& center, float radius) const {
    // A body is outlined by the edges of the triangles Jolt makes of its shape. The level's big meshes are left
    // out: they would hide everything else.
    constexpr int kMaxTriangles = 512;
    constexpr int kBatch = 64;
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> near;
    m_state->system.GetBroadPhaseQuery().CollideSphere(ToJolt(center), radius, near);
    std::vector<BodyOutline> outlines;
    const JPH::BodyLockInterface& locks = m_state->system.GetBodyLockInterface();
    for (const JPH::BodyID id : near.mHits) {
        const JPH::BodyLockRead lock(locks, id);
        if (!lock.Succeeded() || lock.GetBody().GetObjectLayer() == kCharacterLayer) {
            continue;
        }
        const JPH::Body& body = lock.GetBody();
        const JPH::Shape& shape = *body.GetShape();
        if (body.IsStatic() && shape.GetStats().mNumTriangles > 64) {
            continue;
        }
        BodyOutline outline;
        outline.body = BodyId{id.GetIndexAndSequenceNumber()};
        outline.dynamic = body.IsDynamic();
        outline.sleeping = body.IsDynamic() && !body.IsActive();
        JPH::Shape::GetTrianglesContext context;
        shape.GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::Vec3(body.GetCenterOfMassPosition()),
                                body.GetRotation(), JPH::Vec3::sReplicate(1.0f));
        std::array<JPH::Float3, 3 * kBatch> triangles{};
        int total = 0;
        while (total < kMaxTriangles) {
            const int count = shape.GetTrianglesNext(context, kBatch, triangles.data(), nullptr);
            if (count == 0) {
                break;
            }
            for (int triangle = 0; triangle < count; ++triangle) {
                for (int corner = 0; corner < 3; ++corner) {
                    const JPH::Float3& from = triangles[static_cast<std::size_t>(triangle * 3 + corner)];
                    const JPH::Float3& to = triangles[static_cast<std::size_t>(triangle * 3 + (corner + 1) % 3)];
                    outline.lines.emplace_back(from.x, from.y, from.z);
                    outline.lines.emplace_back(to.x, to.y, to.z);
                }
            }
            total += count;
        }
        outlines.push_back(std::move(outline));
    }
    return outlines;
}

std::optional<RayHit> PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                            BodyId ignore) const {
    const JPH::RRayCast ray(JPH::RVec3(ToJolt(origin)), ToJolt(direction * maxDistance));
    JPH::RayCastResult result;
    const JPH::NarrowPhaseQuery& query = m_state->system.GetNarrowPhaseQuery();
    const JPH::IgnoreSingleBodyFilter skip(JPH::BodyID(ignore.value));
    const JPH::BodyFilter all;
    if (!query.CastRay(ray, result, {}, {}, ignore.IsValid() ? static_cast<const JPH::BodyFilter&>(skip) : all)) {
        return std::nullopt;
    }
    RayHit hit;
    hit.distance = result.mFraction * maxDistance;
    hit.position = origin + direction * hit.distance;
    hit.body = BodyId{result.mBodyID.GetIndexAndSequenceNumber()};
    const JPH::BodyLockRead lock(m_state->system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        hit.normal =
            ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
        // A ray that starts behind a triangle hits its back: the normal must still face the ray.
        if (glm::dot(hit.normal, direction) > 0.0f) {
            hit.normal = -hit.normal;
        }
    }
    return hit;
}

CharacterHandle PhysicsWorld::AddCharacter(const CharacterConfig& config, const glm::vec3& position) {
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = State::MakeCharacterShape(config, config.height);
    settings->mMaxSlopeAngle = glm::radians(config.maxSlopeDegrees);
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -config.radius); // Only the lower sphere stands.
    // A body that follows the character, so that rays hit it.
    settings->mInnerBodyShape = settings->mShape;
    settings->mInnerBodyLayer = kCharacterLayer;

    State::Character character;
    character.config = config;
    character.controller =
        new JPH::CharacterVirtual(settings, JPH::RVec3(ToJolt(position)), JPH::Quat::sIdentity(), 0, &m_state->system);
    character.controller->SetCharacterVsCharacterCollision(&m_state->characterCollisions);
    m_state->characterCollisions.Add(character.controller);

    return m_state->characters.Insert(std::move(character));
}

void PhysicsWorld::RemoveCharacter(CharacterHandle character) {
    m_state->characterCollisions.Remove(m_state->Find(character).controller);
    m_state->characters.Remove(character);
}

void PhysicsWorld::MoveCharacter(CharacterHandle id, const glm::vec3& velocity, float step) {
    DAI_PROFILE_SCOPE("physics::MoveCharacter");
    State::Character& character = m_state->Find(id);
    JPH::CharacterVirtual& controller = *character.controller;
    const glm::vec3 start = ToGlm(JPH::Vec3(controller.GetPosition()));
    controller.UpdateGroundVelocity();
    controller.SetLinearVelocity(ToJolt(velocity));

    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, character.config.stepHeight, 0.0f);
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -character.config.stickToFloor, 0.0f);
    JPH::PhysicsSystem& system = m_state->system;
    controller.ExtendedUpdate(step, JPH::Vec3::sZero(), settings, system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                              system.GetDefaultLayerFilter(kMovingLayer), {}, {}, m_state->temp);
    character.velocity = (ToGlm(JPH::Vec3(controller.GetPosition())) - start) / step;
}

void PhysicsWorld::SetCharacterPosition(CharacterHandle character, const glm::vec3& position) {
    State::Character& found = m_state->Find(character);
    found.controller->SetPosition(JPH::RVec3(ToJolt(position)));
    found.velocity = glm::vec3(0.0f);
}

BodyId PhysicsWorld::CharacterBody(CharacterHandle character) const {
    return BodyId{m_state->Find(character).controller->GetInnerBodyID().GetIndexAndSequenceNumber()};
}

glm::vec3 PhysicsWorld::CharacterPosition(CharacterHandle character) const {
    return ToGlm(JPH::Vec3(m_state->Find(character).controller->GetPosition()));
}

glm::vec3 PhysicsWorld::CharacterVelocity(CharacterHandle character) const {
    return m_state->Find(character).velocity;
}

bool PhysicsWorld::IsOnGround(CharacterHandle character) const {
    return m_state->Find(character).controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

bool PhysicsWorld::SetCharacterHeight(CharacterHandle id, float height) {
    State::Character& character = m_state->Find(id);
    JPH::PhysicsSystem& system = m_state->system;
    const JPH::Ref<JPH::Shape> shape = State::MakeCharacterShape(character.config, height);
    if (!character.controller->SetShape(shape, 1.5f * system.GetPhysicsSettings().mPenetrationSlop,
                                        system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                                        system.GetDefaultLayerFilter(kMovingLayer), {}, {}, m_state->temp)) {
        return false;
    }
    character.controller->SetInnerBodyShape(shape);
    return true;
}

} // namespace daidalos::physics
