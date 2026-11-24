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
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
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
#include <unordered_map>
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

// Solver steps of islands that have joints (ADR-0126), against Jolt's default 10 and 2.
constexpr JPH::uint kJointVelocitySteps = 30;
constexpr JPH::uint kJointPositionSteps = 10;

// Below this, a touch that goes on is not worth reporting: neither body is moving.
constexpr float kRestingSpeed = 0.05f;

/// Keeps a joint's dynamic bodies from sleeping, or lets them sleep again.
void KeepAwake(JPH::PhysicsSystem& system, BodyId first, BodyId second, bool awake) {
    for (const BodyId body : {first, second}) {
        if (!body.IsValid()) {
            continue;
        }
        const JPH::BodyLockWrite lock(system.GetBodyLockInterface(), JPH::BodyID(body.value));
        if (lock.Succeeded() && lock.GetBody().IsDynamic()) {
            lock.GetBody().SetAllowSleeping(!awake);
        }
    }
    if (awake) {
        system.GetBodyInterface().ActivateBody(JPH::BodyID(first.value));
    }
}

/// Collects what touched during a step. Jolt may call it from its jobs, so it locks, although the world steps on
/// one thread today.
class ContactCollector final : public JPH::ContactListener {
public:
    /// Bodies held together by a joint pass through each other (ADR-0126): links of a chain overlap where they
    /// meet, a door its frame.
    JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second, JPH::RVec3Arg /*offset*/,
                                          const JPH::CollideShapeResult& /*result*/) override {
        const JPH::CollisionGroup::GroupID group = first.GetCollisionGroup().GetGroupID();
        const bool grouped =
            group != JPH::CollisionGroup::cInvalidGroup && group == second.GetCollisionGroup().GetGroupID();
        return grouped || m_jointed.contains(Pair(first.GetID(), second.GetID()))
                   ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
                   : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    /// A conveyor's surface velocity, or none to stop it (ADR-0127).
    void SetSurfaceVelocity(JPH::BodyID body, const glm::vec3& velocity) {
        if (velocity == glm::vec3(0.0f)) {
            m_surfaces.erase(body.GetIndexAndSequenceNumber());
        } else {
            m_surfaces[body.GetIndexAndSequenceNumber()] = ToJolt(velocity);
        }
    }

    /// Bodies held by more joints than one stay apart until the last goes.
    void Join(JPH::BodyID first, JPH::BodyID second) { ++m_jointed[Pair(first, second)]; }
    void Part(JPH::BodyID first, JPH::BodyID second) {
        const auto found = m_jointed.find(Pair(first, second));
        if (found != m_jointed.end() && --found->second == 0) {
            m_jointed.erase(found);
        }
    }

    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override {
        Carry(first, second, settings);
        Add(first, second, manifold, true);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override {
        Carry(first, second, settings);
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

    /// What lies on a conveyor moves with its surface: Jolt takes the second body's surface velocity relative to
    /// the first's.
    void Carry(const JPH::Body& first, const JPH::Body& second, JPH::ContactSettings& settings) const {
        if (m_surfaces.empty()) {
            return;
        }
        const auto velocity = [this](const JPH::Body& body) {
            const auto found = m_surfaces.find(body.GetID().GetIndexAndSequenceNumber());
            return found == m_surfaces.end() ? JPH::Vec3::sZero() : found->second;
        };
        settings.mRelativeLinearSurfaceVelocity = velocity(second) - velocity(first);
    }

    [[nodiscard]] static std::uint64_t Pair(JPH::BodyID first, JPH::BodyID second) {
        const std::uint32_t low = std::min(first.GetIndexAndSequenceNumber(), second.GetIndexAndSequenceNumber());
        const std::uint32_t high = std::max(first.GetIndexAndSequenceNumber(), second.GetIndexAndSequenceNumber());
        return (static_cast<std::uint64_t>(high) << 32U) | low;
    }

    std::mutex m_mutex;
    std::vector<Contact> m_contacts;
    // Read by Jolt during Update, changed only between steps.
    std::unordered_map<std::uint64_t, int> m_jointed;
    std::unordered_map<std::uint32_t, JPH::Vec3> m_surfaces;
};

} // namespace

struct PhysicsWorld::State {
    struct Joint {
        JointHandle self;
        JointKind kind = JointKind::Point;
        BodyId first;
        BodyId second;
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
        /// The anchors in their bodies' frames, or the second in the world's without a second body.
        glm::vec3 firstAnchor{0.0f};
        glm::vec3 secondAnchor{0.0f};
        float breakForce = 0.0f;
        float force = 0.0f;
        bool broken = false;
    };

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
    core::SlotPool<Joint, JointTag> joints;
    std::vector<JointBreak> breaks;

    /// A point of the world in a body's frame, or as it is without a body.
    [[nodiscard]] glm::vec3 ToBody(BodyId body, const glm::vec3& point) const {
        if (!body.IsValid()) {
            return point;
        }
        const JPH::RMat44 world = system.GetBodyInterface().GetWorldTransform(JPH::BodyID(body.value));
        return ToGlm(JPH::Vec3(world.InversedRotationTranslation() * JPH::RVec3(ToJolt(point))));
    }

    [[nodiscard]] glm::vec3 ToWorld(BodyId body, const glm::vec3& point) const {
        if (!body.IsValid()) {
            return point;
        }
        const JPH::RMat44 world = system.GetBodyInterface().GetWorldTransform(JPH::BodyID(body.value));
        return ToGlm(JPH::Vec3(world * JPH::RVec3(ToJolt(point))));
    }

    void RemoveJoint(Joint& joint) {
        system.RemoveConstraint(joint.constraint);
        joint.constraint = nullptr;
    }
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
    m_state->joints.ForEach([this](State::Joint& joint) { m_state->RemoveJoint(joint); });
    m_state->joints = {};
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
    // A held body waits as a kinematic one, which can become dynamic (ADR-0127).
    JPH::BodyCreationSettings body(shape, JPH::RVec3(ToJolt(transform.translation)), ToJolt(transform.rotation),
                                   desc.held ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic, kMovingLayer);
    body.mAllowDynamicOrKinematic = desc.held;
    body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.01f);
    body.mFriction = std::max(desc.material.friction, 0.0f);
    body.mRestitution = std::clamp(desc.material.restitution, 0.0f, 1.0f);
    body.mLinearDamping = std::max(desc.material.linearDamping, 0.0f);
    body.mAngularDamping = std::max(desc.material.angularDamping, 0.0f);
    body.mMotionQuality = desc.fast ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    if (desc.group != 0) {
        // No group filter: ContactCollector::OnContactValidate keeps a group apart.
        body.mCollisionGroup.SetGroupID(desc.group);
    }
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

void PhysicsWorld::ReleaseBody(BodyId body) {
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID id(body.value);
    if (bodies.GetMotionType(id) == JPH::EMotionType::Kinematic) {
        bodies.SetMotionType(id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    }
}

bool PhysicsWorld::IsHeld(BodyId body) const {
    const JPH::BodyLockRead lock(m_state->system.GetBodyLockInterface(), JPH::BodyID(body.value));
    return lock.Succeeded() && lock.GetBody().IsKinematic() && lock.GetBody().CanBeKinematicOrDynamic();
}

void PhysicsWorld::SetSurfaceVelocity(BodyId body, const glm::vec3& velocity) {
    m_state->contacts.SetSurfaceVelocity(JPH::BodyID(body.value), velocity);
    // What lies on it asleep wakes up and goes.
    if (velocity != glm::vec3(0.0f)) {
        for (const BodyId lying : DynamicBodiesInSphere(BodyPosition(body), 50.0f)) {
            m_state->system.GetBodyInterface().ActivateBody(JPH::BodyID(lying.value));
        }
    }
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
    m_state->contacts.SetSurfaceVelocity(JPH::BodyID(body.value), glm::vec3(0.0f));
    // Its joints first: Jolt keeps them pointing at the body.
    std::vector<JointHandle> held;
    m_state->joints.ForEach([&held, body](const State::Joint& joint) {
        if (joint.first == body || joint.second == body) {
            held.push_back(joint.self);
        }
    });
    for (const JointHandle joint : held) {
        RemoveJoint(joint);
    }
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
    m_state->breaks.clear();
    m_state->system.Update(step, 1, &m_state->temp, &m_state->jobs);
    // How hard each joint pulled, from the impulse its position part needed this step; those pulled past their
    // force break (ADR-0126).
    std::vector<JointHandle> breaking;
    m_state->joints.ForEach([step, &breaking](State::Joint& joint) {
        if (joint.broken) {
            joint.force = 0.0f;
            return;
        }
        float impulse = 0.0f;
        switch (joint.kind) {
        case JointKind::Point:
            impulse = static_cast<JPH::PointConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition().Length();
            break;
        case JointKind::Hinge:
            impulse = static_cast<JPH::HingeConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition().Length();
            break;
        case JointKind::Fixed:
            impulse = static_cast<JPH::FixedConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition().Length();
            break;
        case JointKind::Distance:
            impulse =
                std::abs(static_cast<JPH::DistanceConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition());
            break;
        case JointKind::Slider:
            impulse = std::sqrt(
                static_cast<JPH::SliderConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition().LengthSq());
            break;
        }
        joint.force = impulse / std::max(step, 1e-6f);
        if (joint.breakForce > 0.0f && joint.force > joint.breakForce) {
            breaking.push_back(joint.self);
        }
    });
    for (const JointHandle joint : breaking) {
        BreakJoint(joint);
        m_state->breaks.push_back({.joint = joint, .position = JointAnchor(joint)});
    }
}

core::Result<JointHandle> PhysicsWorld::AddJoint(const JointDesc& desc) {
    const auto invalid = [](std::string message) {
        return tl::unexpected(core::Error{core::ErrorCode::InvalidArgument, kModule, std::move(message)});
    };
    if (!desc.first.IsValid() || !(IsDynamic(desc.first) || IsHeld(desc.first))) {
        return invalid("A joint's first body must be dynamic");
    }
    if (desc.second == desc.first) {
        return invalid("A joint joins two bodies, not one to itself");
    }
    const bool hasAxis = desc.kind == JointKind::Hinge || desc.kind == JointKind::Slider;
    if (hasAxis && glm::length(desc.axis) < 1e-6f) {
        return invalid("A hinge or a slider needs an axis");
    }
    // Jolt measures how far the second body turned or slid from the first: given the opposite axis, its angles,
    // positions, limits and motors are the first body's along the axis as given.
    const JPH::Vec3 axis = hasAxis ? -ToJolt(glm::normalize(desc.axis)) : JPH::Vec3::sAxisY();
    const JPH::Vec3 normal = axis.GetNormalizedPerpendicular();
    const JPH::RVec3 anchor(ToJolt(desc.anchor));
    const JPH::MotorSettings motor(2.0f, 1.0f, desc.motor.strength, desc.motor.strength);

    JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
    switch (desc.kind) {
    case JointKind::Point: {
        auto* point = new JPH::PointConstraintSettings();
        point->mPoint1 = point->mPoint2 = anchor;
        settings = point;
        break;
    }
    case JointKind::Hinge: {
        auto* hinge = new JPH::HingeConstraintSettings();
        hinge->mPoint1 = hinge->mPoint2 = anchor;
        hinge->mHingeAxis1 = hinge->mHingeAxis2 = axis;
        hinge->mNormalAxis1 = hinge->mNormalAxis2 = normal;
        if (desc.limited) {
            hinge->mLimitsMin = std::clamp(desc.lowest, -JPH::JPH_PI, 0.0f);
            hinge->mLimitsMax = std::clamp(desc.highest, 0.0f, JPH::JPH_PI);
        }
        hinge->mMaxFrictionTorque = std::max(desc.friction, 0.0f);
        hinge->mMotorSettings = motor;
        settings = hinge;
        break;
    }
    case JointKind::Distance: {
        auto* distance = new JPH::DistanceConstraintSettings();
        distance->mPoint1 = anchor;
        distance->mPoint2 = JPH::RVec3(ToJolt(desc.secondAnchor));
        if (desc.limited) {
            distance->mMinDistance = std::max(std::min(desc.lowest, desc.highest), 0.0f);
            distance->mMaxDistance = std::max(desc.highest, distance->mMinDistance);
        }
        settings = distance;
        break;
    }
    case JointKind::Fixed: {
        auto* fixed = new JPH::FixedConstraintSettings();
        fixed->mPoint1 = fixed->mPoint2 = anchor;
        fixed->mAxisX1 = fixed->mAxisX2 = JPH::Vec3::sAxisX();
        fixed->mAxisY1 = fixed->mAxisY2 = JPH::Vec3::sAxisY();
        settings = fixed;
        break;
    }
    case JointKind::Slider: {
        auto* slider = new JPH::SliderConstraintSettings();
        slider->mPoint1 = slider->mPoint2 = anchor;
        slider->mSliderAxis1 = slider->mSliderAxis2 = axis;
        slider->mNormalAxis1 = slider->mNormalAxis2 = normal;
        if (desc.limited) {
            slider->mLimitsMin = std::min(desc.lowest, 0.0f);
            slider->mLimitsMax = std::max(desc.highest, 0.0f);
        }
        slider->mMaxFrictionForce = std::max(desc.friction, 0.0f);
        slider->mMotorSettings = motor;
        settings = slider;
        break;
    }
    }
    // Chains hold heavy lamps by light links, which Jolt's default ten velocity steps let stretch: islands with
    // joints are solved harder.
    settings->mNumVelocityStepsOverride = kJointVelocitySteps;
    settings->mNumPositionStepsOverride = kJointPositionSteps;
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    JPH::TwoBodyConstraint* constraint =
        bodies.CreateConstraint(settings, JPH::BodyID(desc.first.value),
                                desc.second.IsValid() ? JPH::BodyID(desc.second.value) : JPH::BodyID());
    if (constraint == nullptr) {
        return invalid("Jolt could not make the joint");
    }
    m_state->system.AddConstraint(constraint);
    bodies.ActivateConstraint(constraint);
    if (desc.second.IsValid()) {
        m_state->contacts.Join(JPH::BodyID(desc.first.value), JPH::BodyID(desc.second.value));
    }

    State::Joint joint;
    joint.kind = desc.kind;
    joint.first = desc.first;
    joint.second = desc.second;
    joint.constraint = constraint;
    joint.firstAnchor = m_state->ToBody(desc.first, desc.anchor);
    joint.secondAnchor =
        m_state->ToBody(desc.second, desc.kind == JointKind::Distance ? desc.secondAnchor : desc.anchor);
    joint.breakForce = std::max(desc.breakForce, 0.0f);
    const JointHandle handle = m_state->joints.Insert(std::move(joint));
    m_state->joints.Get(handle).self = handle;
    SetJointMotor(handle, desc.motor);
    return handle;
}

void PhysicsWorld::RemoveJoint(JointHandle joint) {
    if (State::Joint* found = m_state->joints.TryGet(joint)) {
        if (!found->broken && found->second.IsValid()) {
            m_state->contacts.Part(JPH::BodyID(found->first.value), JPH::BodyID(found->second.value));
        }
        KeepAwake(m_state->system, found->first, found->second, false);
        m_state->RemoveJoint(*found);
        m_state->joints.Remove(joint);
    }
}

std::size_t PhysicsWorld::JointCount() const {
    return m_state->joints.Size();
}

void PhysicsWorld::BreakJoint(JointHandle joint) {
    State::Joint* found = m_state->joints.TryGet(joint);
    if (found == nullptr || found->broken) {
        return;
    }
    found->broken = true;
    found->constraint->SetEnabled(false);
    KeepAwake(m_state->system, found->first, found->second, false);
    if (found->second.IsValid()) {
        m_state->contacts.Part(JPH::BodyID(found->first.value), JPH::BodyID(found->second.value));
    }
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    bodies.ActivateBody(JPH::BodyID(found->first.value));
    if (found->second.IsValid() &&
        bodies.GetMotionType(JPH::BodyID(found->second.value)) == JPH::EMotionType::Dynamic) {
        bodies.ActivateBody(JPH::BodyID(found->second.value));
    }
}

bool PhysicsWorld::IsBroken(JointHandle joint) const {
    const State::Joint* found = m_state->joints.TryGet(joint);
    return found != nullptr && found->broken;
}

void PhysicsWorld::SetJointMotor(JointHandle joint, const JointMotor& motor) {
    State::Joint* found = m_state->joints.TryGet(joint);
    if (found == nullptr) {
        return;
    }
    const auto state = motor.mode == MotorMode::Speed      ? JPH::EMotorState::Velocity
                       : motor.mode == MotorMode::Position ? JPH::EMotorState::Position
                                                           : JPH::EMotorState::Off;
    if (found->kind == JointKind::Hinge) {
        auto* hinge = static_cast<JPH::HingeConstraint*>(found->constraint.GetPtr());
        hinge->GetMotorSettings().SetTorqueLimit(std::max(motor.strength, 0.0f));
        hinge->SetMotorState(state);
        hinge->SetTargetAngularVelocity(motor.mode == MotorMode::Speed ? motor.target : 0.0f);
        hinge->SetTargetAngle(motor.mode == MotorMode::Position ? motor.target : 0.0f);
    } else if (found->kind == JointKind::Slider) {
        auto* slider = static_cast<JPH::SliderConstraint*>(found->constraint.GetPtr());
        slider->GetMotorSettings().SetForceLimit(std::max(motor.strength, 0.0f));
        slider->SetMotorState(state);
        slider->SetTargetVelocity(motor.mode == MotorMode::Speed ? motor.target : 0.0f);
        slider->SetTargetPosition(motor.mode == MotorMode::Position ? motor.target : 0.0f);
    } else {
        return;
    }
    m_state->system.GetBodyInterface().ActivateConstraint(found->constraint);
    // A motor that turns keeps its bodies awake (ADR-0128): stopped for a moment against a load, they would fall
    // asleep, and a motor does not wake what sleeps.
    KeepAwake(m_state->system, found->first, found->second, motor.mode == MotorMode::Speed);
}

float PhysicsWorld::JointPosition(JointHandle joint) const {
    const State::Joint* found = m_state->joints.TryGet(joint);
    if (found == nullptr) {
        return 0.0f;
    }
    if (found->kind == JointKind::Hinge) {
        return static_cast<const JPH::HingeConstraint*>(found->constraint.GetPtr())->GetCurrentAngle();
    }
    if (found->kind == JointKind::Slider) {
        return static_cast<const JPH::SliderConstraint*>(found->constraint.GetPtr())->GetCurrentPosition();
    }
    return 0.0f;
}

float PhysicsWorld::JointForce(JointHandle joint) const {
    const State::Joint* found = m_state->joints.TryGet(joint);
    return found != nullptr ? found->force : 0.0f;
}

glm::vec3 PhysicsWorld::JointAnchor(JointHandle joint) const {
    const State::Joint* found = m_state->joints.TryGet(joint);
    return found != nullptr ? m_state->ToWorld(found->first, found->firstAnchor) : glm::vec3(0.0f);
}

glm::vec3 PhysicsWorld::JointSecondAnchor(JointHandle joint) const {
    const State::Joint* found = m_state->joints.TryGet(joint);
    return found != nullptr ? m_state->ToWorld(found->second, found->secondAnchor) : glm::vec3(0.0f);
}

std::span<const JointBreak> PhysicsWorld::BrokenJoints() const {
    return m_state->breaks;
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
