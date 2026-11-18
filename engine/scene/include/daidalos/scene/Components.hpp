// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/Entity.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/LightAnimation.hpp>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The components that the engine itself reads. Game components live in the game.
namespace daidalos::scene {

/// ECS component: the entity's WorldTransform places the camera, which looks down its local -Z.
struct Camera {
    /// Radians from the bottom of the view to the top. Without one, the engine's view.fov applies.
    std::optional<float> verticalFov;
    float nearPlane = 0.1f;
    /// Depth of field (ADR-0063): the lens's f-number, 0 for none, and the meters it is focused at.
    float aperture = 0.0f;
    float focusDistance = 3.0f;
};

/// ECS component: draws a mesh at the entity's WorldTransform.
struct MeshRenderer {
    renderer::MeshHandle mesh;
    /// Null for the default material: plain white.
    renderer::MaterialHandle material;
    /// False for what must not darken the scene, such as a weapon held in front of the camera.
    bool castsShadows = true;
};

/// ECS component: draws a mesh that follows joints, which are other entities (ADR-0020). The entity's
/// own transform does not move it. A mesh without skin data is drawn rigid, at the entity.
struct SkinnedMeshRenderer {
    renderer::MeshHandle mesh;
    renderer::MaterialHandle material;
    /// The entities of the skin's joints, in the order of the skin.
    std::vector<ecs::Entity> joints;
    /// For each joint, from the space of the mesh to the space of the joint in the bind pose. Shared by
    /// every instance of the model.
    std::shared_ptr<const std::vector<glm::mat4>> inverseBindMatrices;
    bool castsShadows = true;
};

/// ECS component: the world matrix of a drawn entity, or of a joint, when the last view was extracted.
/// ExtractScene adds and updates it, and gives it to the renderer as the previous matrix, from which
/// motion vectors come (ADR-0037).
struct DrawnTransform {
    glm::mat4 matrix{1.0f};
};

/// ECS component: the name of the model node an entity comes from, for the code that builds on a level.
struct NodeName {
    std::string value;
};

/// ECS component: what only the game understands about an entity, from a level's "game" object (ADR-0054) or a model
/// node named "kind;key=value;...". The engine does not read it.
struct Tags {
    std::string kind;
    std::map<std::string, std::string, std::less<>> values;

    /// The value of a key, or empty.
    [[nodiscard]] std::string_view Value(std::string_view key) const {
        const auto found = values.find(key);
        return found == values.end() ? std::string_view{} : std::string_view(found->second);
    }
    [[nodiscard]] bool Has(std::string_view key) const { return values.contains(key); }
    [[nodiscard]] bool operator==(const Tags&) const = default;
};

/// The tags of a node name "kind;key=value;...": everything before the first ';' is the kind.
[[nodiscard]] Tags ParseTags(std::string_view name);

/// ECS component: a light at the entity's WorldTransform, in physical units (ADR-0043). Spot and directional
/// lights shine down the entity's local -Z, like a camera, as in glTF. See renderer::Light.
struct Light {
    renderer::LightType type = renderer::LightType::Point;
    /// Linear color, multiplied by the temperature's.
    glm::vec3 color{1.0f};
    /// Candela for point and spot lights (renderer::PointCandela and SpotCandela convert lumens), lux for
    /// directional lights.
    float intensity = 1.0f;
    /// Kelvin: the color of a black body at this temperature (renderer::ColorTemperature), such as 2700 for
    /// a warm bulb; 0 for none.
    float temperature = 0.0f;
    /// Meters: the light fades to zero there.
    float range = 10.0f;
    /// Spot lights: radians from the axis.
    float innerAngle = glm::radians(20.0f);
    float outerAngle = glm::radians(30.0f);
    /// Meters: the size of the bulb, which softens the shadows (ADR-0039). Zero gives hard shadows.
    float radius = 0.05f;
    /// Directional lights: radians from the center of their disk to its edge, which softens the shadows.
    float angularRadius = 0.0f;
    bool enabled = true;
    /// How a real lamp sends its light around its axis: an IES file, relative to the level's directory, or
    /// empty for every direction alike (ADR-0057). The light points down its entity's -Z.
    std::string profile;
    /// Spot lights: a grey image the light shines through, as a fan or a cage would cut it, relative to the
    /// level's directory; empty for none. Its up is the entity's +Y.
    std::string cookie;
    /// Flicker, failure, power cuts: what changes its brightness over time, and its lamp's glow with it.
    LightAnimation animation;
    [[nodiscard]] bool operator==(const Light&) const = default;
};

/// ECS component: wet air (ADR-0061). The entity's box, from -0.5 to 0.5 scaled by its transform, makes what is
/// inside it wet: porous surfaces darken, water films everything, pools on floors and runs down walls.
/// ECS component: a room or a doorway for portal visibility (ADR-0089). Its box, from -0.5 to 0.5 scaled by
/// its transform, is a cell: where two cells overlap is the opening between them.
struct VisibilityCell {
    bool enabled = true;
    [[nodiscard]] bool operator==(const VisibilityCell&) const = default;
};

struct WetVolume {
    /// 0 dry, 1 soaked; it falls by dryingRate.
    float wetness = 1.0f;
    /// The fraction of the floor under still water.
    float puddles = 0.0f;
    /// Meters above the box's bottom up to which water pools.
    float puddleHeight = 0.05f;
    /// Rain drops per square meter per second, which ripple the puddles.
    float rain = 0.0f;
    /// How much water runs down walls.
    float streaks = 0.0f;
    /// Wetness lost per minute, as the place dries; 0 stays wet.
    float dryingRate = 0.0f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const WetVolume&) const = default;
};

/// ECS component: a decal (ADR-0060). The entity's box, from -0.5 to 0.5 scaled by its transform, projects an
/// image along its -Z onto what is inside it: blood on a wall, mold in a corner, grime on a floor.
struct Decal {
    /// The image, with its coverage in alpha, relative to the level's directory.
    std::string texture;
    /// A normal map over the same image, or empty.
    std::string normal;
    glm::vec3 color{1.0f};
    float opacity = 1.0f;
    /// What the surface becomes where it is covered: wet blood is smooth, grime rough.
    float roughness = 0.6f;
    float metallic = 0.0f;
    /// Surfaces turned further than this cosine from facing it fade out.
    float angleFade = 0.3f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const Decal&) const = default;
};

/// ECS component: a Decal's textures, loaded when it was made.
struct DecalTextures {
    rhi::TextureHandle albedo;
    rhi::TextureHandle normal;
};

/// ECS component: which rays see the meshes of an entity and of what is under it (ADR-0059). A figure only in
/// mirrors: no camera, no bounced light. A shadow with no caster: shadows only.
struct RayVisibility {
    bool camera = true;
    bool shadows = true;
    bool reflections = true;
    bool indirect = true;
    [[nodiscard]] bool operator==(const RayVisibility&) const = default;
};

/// ECS component: the textures of a Light's profile and cookie, loaded when it was made.
struct LightTextures {
    rhi::TextureHandle profile;
    rhi::TextureHandle cookie;
};

/// ECS component: how bright an animated light is this frame, from 0 to 1 (AnimateLights).
struct LightState {
    float factor = 1.0f;
};

/// ECS component: what the emission of the meshes under an entity is multiplied by this frame: the lamp of an
/// animated light glows as its light shines (ADR-0057).
struct EmissiveScale {
    float value = 1.0f;
};

/// ECS component: a box of fog at the entity's WorldTransform (ADR-0051), added to the atmosphere's: the
/// cube from -0.5 to 0.5, scaled by the transform to the box's size. See renderer::FogVolume.
struct FogVolume {
    /// Fraction of the light lost per meter, at the heart of the box.
    float density = 0.1f;
    /// Of the light lost, the fraction scattered rather than absorbed, per color.
    glm::vec3 albedo{0.9f};
    /// The fog thins to nothing towards the faces, over this fraction of the box's half-size.
    float edge = 0.3f;
    /// Meters over which the density halves, going up from the bottom of the box: fog that lies on the
    /// ground. The bottom face then does not thin, as the fog rests on the floor there. 0 for the same
    /// density at every height.
    float falloffHeight = 0.0f;
    /// How much the density varies, from 0 to 1, with a noise that changes every 1 / noiseFrequency meters
    /// and moves at noiseVelocity, in meters per second: steam rises, dust drifts.
    float noise = 0.0f;
    float noiseFrequency = 1.0f;
    glm::vec3 noiseVelocity{0.0f};
    /// Light the fog gives off, per color, as renderer::FogVolume::glow: a toxic gas glowing in the dark.
    glm::vec3 glow{0.0f};
    bool enabled = true;
    [[nodiscard]] bool operator==(const FogVolume&) const = default;
};

/// ECS component: a box of wind at the entity's WorldTransform (ADR-0053), which carries particles by their
/// drag: the cube from -0.5 to 0.5, scaled by the transform to the box's size. See renderer::WindVolume.
struct WindVolume {
    /// Meters per second, in the entity's frame: a draft blowing along a corridor turns with it.
    glm::vec3 velocity{0.0f};
    float gust = 0.0f;
    float gustFrequency = 0.2f;
    float edge = 0.3f;
    bool enabled = true;
    [[nodiscard]] bool operator==(const WindVolume&) const = default;
};

/// ECS component: pulls particles towards the entity's position and turns them around its +Y axis
/// (ADR-0053). See renderer::ParticleAttractor.
struct ParticleAttractor {
    /// The only effect it moves, or null for every effect.
    renderer::ParticleEffectHandle effect;
    float strength = 1.0f;
    float vortex = 0.0f;
    float radius = 0.0f;
    bool enabled = true;
};

/// A burst of an emitter (ADR-0053): count particles at time seconds after it starts, then every interval
/// seconds, repeats times in all (0: for ever). Without an interval, it fires once.
struct ParticleBurst {
    float time = 0.0f;
    std::uint32_t count = 1;
    float interval = 0.0f;
    std::uint32_t repeats = 0;
};

/// The light an emitter gives off (ADR-0053): a fire lights its room. One point light per emitter, a sphere of
/// radius, as bright as the emitter is active: full while it emits, fading over fade seconds once it stops,
/// and flickering flickerRate times a second by up to flickerDepth of itself.
struct EmitterLight {
    /// Linear color, multiplied by the temperature's (kelvin, 0 for none).
    glm::vec3 color{1.0f};
    float temperature = 0.0f;
    /// Candela.
    float intensity = 10.0f;
    float range = 5.0f;
    float radius = 0.1f;
    /// Where the light is, in the emitter's frame: a flame's light is above its base.
    glm::vec3 offset{0.0f};
    float fade = 0.5f;
    float flickerRate = 0.0f;
    float flickerDepth = 0.0f;
};

/// How an emitter emits (ADR-0053): an effect file gives its emitters these, which each can change.
struct ParticleEmission {
    /// Particles per second.
    float rate = 10.0f;
    /// Particles per meter the emitter travels, spread along the way: trails.
    float ratePerMeter = 0.0f;
    std::vector<ParticleBurst> bursts;
    /// The light it gives off, if any.
    std::optional<EmitterLight> light;
    /// Beyond this many meters from the camera, it emits nothing; its light stays (ADR-0053).
    float distance = 50.0f;
    /// Meters around it that its particles reach: it emits while that sphere may be in view.
    float radius = 2.0f;
};

/// The triangles of a mesh that particles appear on, drawn by area (ADR-0053): what burns, what crumbles.
/// Positions are in the frame of the entity that emits.
struct MeshSurface {
    /// Three corners per triangle.
    std::vector<glm::vec3> corners;
    /// The area of the triangles up to and including each, for drawing one by area.
    std::vector<float> cumulativeArea;
};

/// The surface of a mesh's triangles, or nothing if they have no area.
[[nodiscard]] std::shared_ptr<const MeshSurface> MakeMeshSurface(std::span<const glm::vec3> positions,
                                                                 std::span<const std::uint32_t> indices);

/// A point on a surface and the normal there, from three random numbers in [0, 1): the first picks the
/// triangle by area, the other two the point in it, evenly.
struct SurfacePoint {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};
[[nodiscard]] SurfacePoint SamplePoint(const MeshSurface& surface, float pick, float u, float v);

/// ECS component: emits particles of an effect at the entity's WorldTransform, at a steady rate, as it
/// moves, and in bursts. The effect's directions are in the entity's frame (ADR-0025). Disabling it starts
/// it over: enabled again, it fires its bursts again.
struct ParticleEmitter {
    renderer::ParticleEffectHandle effect;
    ParticleEmission emission;
    bool enabled = true;
    /// If set, particles appear on its triangles, each leaving along the normal where it appears (the
    /// effect's +Y), instead of at the entity.
    std::shared_ptr<const MeshSurface> surface;
    /// Counts the points drawn on the surface, for the next random ones.
    std::uint32_t drawn = 0;
    /// The fraction of a particle carried over to the next frame.
    float pending = 0.0f;
    /// Seconds since it started, for its bursts.
    float age = 0.0f;
    /// Where it was last frame, once it has emitted.
    std::optional<glm::vec3> lastPosition;
    /// From 1 while it emits to 0 once it has stopped for a while: how bright its light is.
    float activity = 0.0f;
};

/// ECS component: a box where it rains or snows, at the entity's WorldTransform (ADR-0053): the cube from -0.5 to
/// 0.5, scaled to the zone's size; a turned box counts as its bounds. Its effect's particles appear at its top,
/// only over the part of it within reach of the camera, so a level with one courtyard spends nothing on the
/// rest. The effect falls along its direction, and its spawn box spans the zone where its X and Z are 1.
struct WeatherZone {
    renderer::ParticleEffectHandle effect;
    /// Particles per square meter per second.
    float rate = 50.0f;
    /// Meters from the camera, across, within which it rains.
    float reach = 15.0f;
    bool enabled = true;
    /// The fraction of a particle carried over to the next frame.
    float pending = 0.0f;
};

/// The shape of a ribbon (ADR-0053).
enum class RibbonShape : std::uint8_t {
    /// Straight from the emitter to its target: a beam, a laser.
    Line,
    /// The line split again and again at its middle, each middle pushed aside, with branches; a new shape
    /// every refresh seconds: an electric arc, lightning.
    Bolt,
    /// Where the emitter has been over the last duration seconds: a trail, a wisp.
    Trail,
};

/// What a ribbon looks like and how it is built, as a ribbon file gives it (`.ribbon.json`, ADR-0053).
/// Distances are meters, times seconds.
struct RibbonEffect {
    /// Repeats along the ribbon and spans it across; null for plain color.
    rhi::TextureHandle texture;
    bool additive = true;
    /// Meters over which it fades into a surface behind it.
    float softness = 0.05f;
    RibbonShape shape = RibbonShape::Line;
    /// Linear color and opacity, and width, along the ribbon: from its start (0) to its end (1). A trail
    /// starts at the emitter, with its newest point.
    std::vector<renderer::ParticleColorKey> color{{0.0f, glm::vec4(1.0f)}};
    std::vector<renderer::ParticleCurveKey> width{{0.0f, 0.05f}};
    /// A line or bolt without a target goes this far along the emitter's +Y.
    float length = 1.0f;
    /// Meters of ribbon per repeat of the texture, and repeats per second it moves along, towards the end.
    float tiling = 1.0f;
    float scroll = 0.0f;
    /// Bolt: times the line is split, how far a middle is pushed aside at the first split (a fraction of
    /// the length, halved at each split after), the chance that a middle starts a branch, the seconds between
    /// new shapes, and how much dimmer a new shape can be.
    std::uint32_t generations = 5;
    float displacement = 0.15f;
    float branches = 0.0f;
    float refresh = 0.05f;
    float flicker = 0.0f;
    /// Trail: how long its points last, and how far apart they are kept.
    float duration = 0.5f;
    float spacing = 0.05f;
    /// The light it gives off while enabled (EmitterLight), at its middle, or a trail's newest point.
    std::optional<EmitterLight> light;
};

/// ECS component: a ribbon at the entity's WorldTransform, built each frame (ADR-0053). Disabling it hides
/// it, and fades its light.
struct RibbonEmitter {
    std::shared_ptr<const RibbonEffect> effect;
    /// Where a line or bolt ends, if alive: the entity's world position.
    ecs::Entity target;
    bool enabled = true;

    /// Seconds since it started: the texture's scroll, the light's flicker.
    float age = 0.0f;
    /// A bolt's shape, as lines across the frame of its line (along it from 0 to 1, aside in fractions of
    /// its length): the bolt first, then its branches. Kept between refreshes.
    std::vector<std::vector<glm::vec3>> bolt;
    float sinceShape = std::numeric_limits<float>::infinity();
    float brightness = 1.0f;
    std::uint32_t shapes = 0;
    /// A trail's points, newest first, with the age at which each was left.
    struct TrailPoint {
        glm::vec3 position{0.0f};
        float age = 0.0f;
    };
    std::deque<TrailPoint> trail;
    /// From 1 while enabled to 0 once it has been disabled a while: how bright its light is.
    float activity = 0.0f;
};

/// ECS component: a storm's lightning (ADR-0053). Now and then a bolt strikes inside the box of the entity's
/// WorldTransform, from its top face down to its bottom one, a light flashes, and thunder follows, as late as
/// sound takes to come from the bolt to the camera (Scene::TakeThunder).
struct Lightning {
    /// The bolt's ribbon; its length and target are the strike's.
    std::shared_ptr<const RibbonEffect> bolt;
    /// A light entity, the moon through the windows, which flashes: its light times up to flash.
    ecs::Entity light;
    float flash = 30.0f;
    /// Seconds between strikes, picked between the two.
    glm::vec2 interval{6.0f, 15.0f};
    /// How long a strike lasts, its flickers included.
    float duration = 0.5f;
    bool enabled = true;

    /// Seconds to the next strike, and since the last.
    float untilStrike = -1.0f;
    float sinceStrike = std::numeric_limits<float>::infinity();
    std::uint32_t strikes = 0;
    /// The last strike's ends, and when its flickers come, in seconds from its start.
    glm::vec3 top{0.0f};
    glm::vec3 bottom{0.0f};
    glm::vec3 flickers{0.0f};
    std::vector<std::vector<glm::vec3>> shape;
    /// Thunder on its way: where it came from, and seconds until it arrives.
    struct Rumble {
        glm::vec3 position{0.0f};
        float delay = 0.0f;
    };
    std::vector<Rumble> rumbles;
};

/// Thunder reaching the camera (Lightning): where the bolt was, and how far.
struct Thunder {
    glm::vec3 position{0.0f};
    float distance = 0.0f;
};

/// ECS component: the entity's LocalTransform moves a kinematic body of the scene's physics. The entity
/// must be at the root of the hierarchy, or under parents that do not move.
struct KinematicBody {
    physics::BodyId id;
};

/// ECS component: a voice of the scene's sounds that follows the entity.
/// ECS component: a box that plays a sequence when something that triggers enters it (ADR-0071). The box
/// is the unit cube the entity's transform places, as fog and wet volumes are.
struct Trigger {
    /// The .sequence.json file, relative to the asset root.
    std::string sequence;
    /// Once: it fires the first time only; otherwise each time something enters.
    bool once = true;
    bool enabled = true;
    [[nodiscard]] bool operator==(const Trigger&) const = default;
};

/// What a loose object is (ADR-0073): a box of this size, in meters, around a center in the entity's frame, and
/// its mass in kilograms. A level entity with one falls, tumbles, and can be pushed, picked up and thrown.
struct Body {
    glm::vec3 size{0.5f};
    glm::vec3 center{0.0f};
    /// Kilograms. 0 makes a static box: it never moves, and stops the player, shots and sound (ADR-0083), as
    /// a prop of the level placed by a parent does too.
    float mass = 10.0f;
    [[nodiscard]] bool operator==(const Body&) const = default;
};

/// ECS component: the static box of a level entity whose body has no mass (ADR-0083): its world-space corners
/// and the physics body made of them, which the game also gives to navigation and sound.
struct StaticBox {
    physics::BodyId body;
    std::array<glm::vec3, 8> corners{};
};

/// The twelve triangles of a box's eight corners, as StaticBox orders them (bit 0 is +X, 1 is +Y, 2 is +Z).
inline constexpr std::array<std::uint32_t, 36> kBoxIndices{0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                                           2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};

/// ECS component: the physics body that moves a loose object; the scene puts the entity where it is after each
/// step. The entity must have no parent.
struct RigidBody {
    physics::BodyId body;
};

/// ECS component: what sets off triggers, as the player.
struct TriggerActivator {};

/// ECS component: whether something is inside a trigger, and whether it has fired: the trigger's state.
struct TriggerState {
    bool inside = false;
    bool fired = false;
};

struct SoundEmitter {
    audio::VoiceHandle voice;
    /// Its volume, which a light it follows scales.
    float volume = 1.0f;
    /// It sounds as bright as the entity's light shines (LightState, ADR-0068): a tube's hum dies with it.
    bool followsLight = false;
};

} // namespace daidalos::scene
