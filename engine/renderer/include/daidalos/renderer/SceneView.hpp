// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/renderer/Particles.hpp>
#include <daidalos/rhi/Types.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace daidalos::renderer {

struct MeshTag;
/// A mesh owned by the Renderer.
using MeshHandle = core::Handle<MeshTag>;

struct MaterialTag;
/// A material owned by the Renderer.
using MaterialHandle = core::Handle<MaterialTag>;

struct CameraView {
    /// World space to camera space.
    glm::mat4 view{1.0f};
    /// Radians from the bottom of the view to the top. Without one, the renderer's field of view applies
    /// (RendererConfig::fieldOfView), across the view.
    std::optional<float> verticalFov;
    float nearPlane = 0.0f;
};

/// Marks a draw that is not skinned.
inline constexpr std::uint32_t kNoJoints = 0xFFFFFFFF;

/// Names what is drawn or lights from one frame to the next, such as its entity: the samples of lights that
/// a pixel keeps from frame to frame follow it (ADR-0044). 0 names nothing, and the position in the view
/// stands in for it.
using SceneId = std::uint64_t;

struct DrawItem {
    MeshHandle mesh;
    glm::mat4 world{1.0f};
    MaterialHandle material;
    /// Index into SceneView::jointMatrices of the first matrix of a skinned draw, whose world matrix
    /// is then ignored.
    std::uint32_t firstJoint = kNoJoints;
    /// Stops the rays of shadows (ADR-0035). The first-person weapons do not.
    bool castsShadows = true;
    /// Where the draw was in the previous frame drawn, for motion vectors (ADR-0037). Without one, it
    /// stood still.
    std::optional<glm::mat4> previousWorld;
    /// The same from frame to frame, if its material is an emissive light.
    SceneId id = 0;
};

/// The fog that fills the level (ADR-0051): a participating medium, the same everywhere within the view
/// distance of the camera. Light scatters in it, lit by the frame's lights, and is lost through it: what no
/// light reaches stays dark. One medium holds for every fog, the volumes' included, whose phase it sets.
struct Fog {
    /// Fraction of the light lost per meter, by absorption and scattering (the extinction coefficient); 0
    /// for clear air.
    float density = 0.0f;
    /// Of the light lost, the fraction that scatters rather than is absorbed, per color: its albedo.
    glm::vec3 albedo{0.9f};
    /// How the light scatters, from -1 back towards where it came from to 1 straight on, 0 the same in every
    /// direction (Henyey and Greenstein 1941), for every fog: dust and steam send most of it forwards.
    float anisotropy = 0.3f;
};

/// A box of fog, added to the level's (ADR-0051): dust in a room, steam from a pipe. The box is the cube
/// from -0.5 to 0.5 in world's space: its scale is the box's size.
struct FogVolume {
    glm::mat4 world{1.0f};
    /// As Fog::density, at the heart of the box.
    float density = 0.0f;
    glm::vec3 albedo{0.9f};
    /// The fog thins to nothing towards the faces, over this fraction of the box's half-size: 0 ends sharply
    /// at the faces, 1 thins from the center.
    float edge = 0.3f;
    /// Meters over which the density halves, going up the box's Y axis from its bottom face, which then
    /// does not thin: fog that lies on the ground. 0 for the same density at every height.
    float falloffHeight = 0.0f;
    /// How much the density varies, from 0 (even) to 1 (from none to twice as dense), with a noise that
    /// moves at noiseVelocity, in meters per second, and changes every 1 / noiseFrequency meters.
    float noise = 0.0f;
    float noiseFrequency = 1.0f;
    glm::vec3 noiseVelocity{0.0f};
};

/// The level's fog and sky. The sky shows where nothing is drawn, and lights the scene through the rays of
/// indirect light that leave it (ADR-0040): no light comes from nowhere.
struct Atmosphere {
    Fog fog;
    /// A cube texture (rhi::TextureDesc::cube) seen where nothing is drawn, and the light that rays which
    /// leave the scene find (ADR-0036). Null for none.
    rhi::TextureHandle sky;
    /// Multiplies the sky's colors, which are linear light, into its luminance in nits (ADR-0043).
    float skyIntensity = 1.0f;
    /// Radians the sky is turned about +Y, counter-clockwise seen from above, as a yaw: pi puts what was
    /// ahead behind (ADR-0040).
    float skyYaw = 0.0f;
    /// The camera's exposure value at ISO 100 (ADR-0043): each step up halves the light that reaches the
    /// screen. About 3 for a dim interior, 8 for a bright one, 15 for a sunny day outside. The renderer's
    /// exposure compensation is added in stops, the other way. With auto-exposure, where the eye starts.
    float ev100 = 0.0f;
    /// Auto-exposure (ADR-0048): the eye adapts to the light between ev100Min, the darkest the level may
    /// look, and ev100Max, the brightest. Off, the exposure stays at ev100. The range is the level's
    /// darkness: a dark corridor stays dark when the eye cannot open any further. Up to 8 stops wide.
    bool autoExposure = false;
    float ev100Min = 0.0f;
    float ev100Max = 0.0f;
    /// Air moving through the whole level, meters per second, which carries particles by their drag
    /// (ADR-0053): through a broken roof, a courtyard. It gusts by up to windGust of itself, 0 to 1,
    /// windGustFrequency times a second.
    glm::vec3 wind{0.0f};
    float windGust = 0.0f;
    float windGustFrequency = 0.2f;
};

/// Air moving through a box (ADR-0053): a draft under a door, the blast of a fan. The box is the cube from
/// -0.5 to 0.5 in world's space: its scale is the box's size. Its wind adds to the level's.
struct WindVolume {
    glm::mat4 world{1.0f};
    /// Meters per second, in the world's frame.
    glm::vec3 velocity{0.0f};
    /// How much it gusts, from 0 (steady) to 1, and how many times a second.
    float gust = 0.0f;
    float gustFrequency = 0.2f;
    /// The wind fades to nothing towards the faces over this fraction of the box's half-size.
    float edge = 0.3f;
};

/// A point that pulls particles towards it and turns them around an axis (ADR-0053): flies around a corpse,
/// moths around a lamp.
struct ParticleAttractor {
    glm::vec3 position{0.0f};
    /// The axis it turns particles around, counterclockwise looking down it.
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    /// The only effect it moves, or null for every effect.
    ParticleEffectHandle effect;
    /// Meters per second squared towards it, and around its axis.
    float strength = 1.0f;
    float vortex = 0.0f;
    /// Where its pull fades to nothing, in meters; 0 reaches everywhere.
    float radius = 0.0f;
};

/// A directional light is infinitely far, as the sun or the moon: it shines along its direction everywhere,
/// at the same strength, and has no position, range or falloff (ADR-0040).
enum class LightType { Point, Spot, Directional };

/// A light of the frame, in physical units (ADR-0043): its intensity in candela, or its illuminance in lux
/// for a directional light. Light falls off with the inverse square of the distance, and smoothly to zero
/// at the range.
struct Light {
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    /// Spot and directional lights shine along it. Normalized.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    /// Linear color, already multiplied by the intensity: candela, or lux for a directional light.
    glm::vec3 color{1.0f};
    /// Meters: the light fades to zero there.
    float range = 10.0f;
    /// Spot lights: radians from the axis, full light inside the inner angle, none outside the outer.
    float innerAngle = 0.0f;
    float outerAngle = 0.0f;
    /// Meters: the light is a sphere of this radius, whose shadows are soft (ADR-0039). Zero is a point,
    /// with hard shadows.
    float radius = 0.0f;
    /// Directional lights: radians from the center of the disk the light comes from to its edge, which
    /// softens the shadows as radius does. The moon is 0.0045.
    float angularRadius = 0.0f;
    /// The same from frame to frame.
    SceneId id = 0;
};

/// What the renderer draws in one frame. It holds copies, so that rendering never touches the ECS
/// (OVERVIEW §10.12).
struct SceneView {
    /// Nothing is drawn without a camera.
    std::optional<CameraView> camera;
    std::vector<DrawItem> draws;
    /// For each joint of the skinned draws: its world matrix times its inverse bind matrix.
    std::vector<glm::mat4> jointMatrices;
    /// The same in the previous frame drawn, for motion vectors: as many as jointMatrices, or none when
    /// the joints stood still.
    std::vector<glm::mat4> previousJointMatrices;
    Atmosphere atmosphere;
    std::vector<Light> lights;
    /// Boxes of fog, added to the atmosphere's (ADR-0051).
    std::vector<FogVolume> fogVolumes;
    /// New particles of the frame (ADR-0025).
    std::vector<ParticleSpawn> particleSpawns;
    /// What moves particles besides gravity and the level's wind (ADR-0053).
    std::vector<WindVolume> windVolumes;
    std::vector<ParticleAttractor> particleAttractors;
    /// Seconds since the previous frame, by which particles move.
    float timeStep = 0.0f;
    /// The view does not follow from the previous one, after a teleport or a change of scene: nothing
    /// is reprojected from earlier frames (ADR-0037).
    bool cut = false;
};

} // namespace daidalos::renderer
