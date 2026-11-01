// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>

// clang-format off
// Jolt.h must come first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <format>
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

// Two layers are enough: what never moves does not collide with itself.
constexpr JPH::ObjectLayer kStaticLayer = 0;
constexpr JPH::ObjectLayer kMovingLayer = 1;
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
        system.SetGravity(JPH::Vec3::sZero()); // The game applies its own to characters.
    }

    [[nodiscard]] Character& Find(CharacterId id) {
        const auto found = characters.find(id.value);
        DAI_ASSERT(found != characters.end(), "Unknown character");
        return found->second;
    }

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
    std::unordered_map<std::uint32_t, Character> characters;
    std::uint32_t nextCharacter = 0;
};

PhysicsWorld::PhysicsWorld() {
    AcquireJolt();
    m_state = std::make_unique<State>();
}

PhysicsWorld::~PhysicsWorld() {
    if (!m_state) {
        return;
    }
    m_state->characters.clear();
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
                                                 const math::Transform& transform) {
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
    const JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(ToJolt(transform.translation)),
                                         ToJolt(transform.rotation), JPH::EMotionType::Static, kStaticLayer);
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

void PhysicsWorld::RemoveBody(BodyId body) {
    JPH::BodyInterface& bodies = m_state->system.GetBodyInterface();
    const JPH::BodyID id(body.value);
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}

std::size_t PhysicsWorld::BodyCount() const {
    return m_state->system.GetNumBodies();
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
    m_state->system.Update(step, 1, &m_state->temp, &m_state->jobs);
}

std::optional<RayHit> PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction,
                                            float maxDistance) const {
    const JPH::RRayCast ray(JPH::RVec3(ToJolt(origin)), ToJolt(direction * maxDistance));
    JPH::RayCastResult result;
    const JPH::NarrowPhaseQuery& query = m_state->system.GetNarrowPhaseQuery();
    if (!query.CastRay(ray, result)) {
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

CharacterId PhysicsWorld::AddCharacter(const CharacterConfig& config, const glm::vec3& position) {
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = State::MakeCharacterShape(config, config.height);
    settings->mMaxSlopeAngle = glm::radians(config.maxSlopeDegrees);
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -config.radius); // Only the lower sphere stands.

    State::Character character;
    character.config = config;
    character.controller =
        new JPH::CharacterVirtual(settings, JPH::RVec3(ToJolt(position)), JPH::Quat::sIdentity(), 0, &m_state->system);
    character.controller->SetCharacterVsCharacterCollision(&m_state->characterCollisions);
    m_state->characterCollisions.Add(character.controller);

    const CharacterId id{m_state->nextCharacter++};
    m_state->characters.emplace(id.value, std::move(character));
    return id;
}

void PhysicsWorld::RemoveCharacter(CharacterId character) {
    m_state->characterCollisions.Remove(m_state->Find(character).controller);
    m_state->characters.erase(character.value);
}

void PhysicsWorld::MoveCharacter(CharacterId id, const glm::vec3& velocity, float step) {
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

void PhysicsWorld::SetCharacterPosition(CharacterId character, const glm::vec3& position) {
    State::Character& found = m_state->Find(character);
    found.controller->SetPosition(JPH::RVec3(ToJolt(position)));
    found.velocity = glm::vec3(0.0f);
}

glm::vec3 PhysicsWorld::CharacterPosition(CharacterId character) const {
    return ToGlm(JPH::Vec3(m_state->Find(character).controller->GetPosition()));
}

glm::vec3 PhysicsWorld::CharacterVelocity(CharacterId character) const {
    return m_state->Find(character).velocity;
}

bool PhysicsWorld::IsOnGround(CharacterId character) const {
    return m_state->Find(character).controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

bool PhysicsWorld::SetCharacterHeight(CharacterId id, float height) {
    State::Character& character = m_state->Find(id);
    JPH::PhysicsSystem& system = m_state->system;
    const JPH::Ref<JPH::Shape> shape = State::MakeCharacterShape(character.config, height);
    return character.controller->SetShape(shape, 1.5f * system.GetPhysicsSettings().mPenetrationSlop,
                                          system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                                          system.GetDefaultLayerFilter(kMovingLayer), {}, {}, m_state->temp);
}

void SyncKinematicBodies(ecs::World& world, PhysicsWorld& physics, float step) {
    world.Each<KinematicBody, ecs::LocalTransform>(
        [&](ecs::Entity /*entity*/, const KinematicBody& body, const ecs::LocalTransform& transform) {
            physics.MoveKinematic(body.id, transform.translation, transform.rotation, step);
        });
}

} // namespace daidalos::physics
