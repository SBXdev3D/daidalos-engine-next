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
    /// Depth of field (ADR-0063), with temporal antialiasing: the lens's f-number, as a camera's on a
    /// full-frame sensor, 0 for none (everything sharp), and the distance in meters it is focused at.
    float aperture = 0.0f;
    float focusDistance = 3.0f;
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
    /// Multiplies its material's emission, and the light it gives: an animated lamp's glow (ADR-0057).
    float emissiveScale = 1.0f;
    /// Which rays see it besides shadow rays (castsShadows) (ADR-0059): the camera, reflections, and the rays
    /// of bounced light. A figure seen only in a mirror sees neither the camera nor bounced light.
    bool seenByCamera = true;
    bool seenInReflections = true;
    bool seenByIndirect = true;
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

/// A decal (ADR-0060): the box from -0.5 to 0.5 of world projects an image along its -Z onto the surfaces inside
/// it, in the material function, so that it shows in reflections and bounced light too. Its scale is the box's
/// size: the image covers X and Y, Z is how deep it reaches.
struct Decal {
    glm::mat4 world{1.0f};
    /// The image, with its coverage in alpha (sRGB colors).
    rhi::TextureHandle albedo;
    /// A normal map over the same image, or null.
    rhi::TextureHandle normal;
    /// Multiplies the image.
    glm::vec3 color{1.0f};
    /// Multiplies its coverage: 0 shows nothing.
    float opacity = 1.0f;
    /// What the surface becomes where it covers it.
    float roughness = 0.6f;
    float metallic = 0.0f;
    /// Surfaces turned further than this cosine from facing it fade out: a decal does not run down the sides.
    float angleFade = 0.3f;
};

/// A box of wet air (ADR-0061): what is inside it is wet, the box from -0.5 to 0.5 of world. Porous materials
/// darken, everything gets a film of water; on what faces up near the box's bottom, water pools in low spots;
/// on walls, streaks of water run down.
struct WetVolume {
    glm::mat4 world{1.0f};
    /// 0 dry, 1 soaked.
    float wetness = 1.0f;
    /// The fraction of the floor under still water, 0 to 1.
    float puddles = 0.0f;
    /// Meters above the box's bottom up to which water pools.
    float puddleHeight = 0.05f;
    /// Rain drops per square meter per second, which ripple the puddles.
    float rain = 0.0f;
    /// How much water runs down walls, 0 to 1.
    float streaks = 0.0f;
};

/// A ripple on still water that the game started: a step, an impact (ADR-0061).
struct Ripple {
    glm::vec3 position{0.0f};
    /// Seconds since it started.
    float age = 0.0f;
    float strength = 1.0f;
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
    /// Light the fog gives off, per color: the radiance of the box's fog where it is too thick to see
    /// through, as a toxic gas glows in the dark. Each meter adds this times its density. 0 for none.
    glm::vec3 glow{0.0f};
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
    /// How a real lamp sends its light around its direction (ADR-0057): a row of its intensity by the angle
    /// from it, from 0 to 180 degrees (scene::AssetLibrary::LoadLightProfile); null for every direction alike.
    rhi::TextureHandle profile;
    /// Spot lights: a grey image the light shines through, over its cone; null for none.
    rhi::TextureHandle cookie;
    /// The cookie's up, across the light's direction.
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

/// What the renderer draws in one frame. It holds copies, so that rendering never touches the ECS
/// (OVERVIEW §10.12).
/// A rectangle of the window, in pixels from its top-left corner.
struct ViewRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    [[nodiscard]] bool operator==(const ViewRect&) const = default;
};

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
    /// Decals, the nearest kMaxDecals of which are drawn (ADR-0060).
    std::vector<Decal> decals;
    /// Boxes of wet air, kMaxWetVolumes of them at most, and ripples on their water (ADR-0061).
    std::vector<WetVolume> wetVolumes;
    std::vector<Ripple> ripples;
    /// New particles of the frame (ADR-0025).
    std::vector<ParticleSpawn> particleSpawns;
    /// Ribbons of the frame, built by the CPU (ADR-0053).
    std::vector<Ribbon> ribbons;
    /// What moves particles besides gravity and the level's wind (ADR-0053).
    std::vector<WindVolume> windVolumes;
    std::vector<ParticleAttractor> particleAttractors;
    /// Seconds since the previous frame, by which particles move.
    float timeStep = 0.0f;
    /// The view does not follow from the previous one, after a teleport or a change of scene: nothing
    /// is reprojected from earlier frames (ADR-0037).
    bool cut = false;
    /// Where in the window the scene is drawn, such as an editor's viewport between its panels (ADR-0055): the
    /// scene renders at that size, and the rest of the window is black under the interface. Without one, or
    /// if it is smaller than 8 pixels across, the whole window.
    std::optional<ViewRect> viewport;
};

} // namespace daidalos::renderer
