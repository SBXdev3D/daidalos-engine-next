// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#ifndef DAIDALOS_MESH_TYPES_HLSLI
#define DAIDALOS_MESH_TYPES_HLSLI

// Each structure matches its C++ twin in engine/renderer/src/MeshPass.cpp.

// Indices only: everything else is in storage buffers.
struct MeshPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint instanceIndex;
    uint materialIndex;
    uint jointBuffer;
    uint shadowIndex; // The shadow map being drawn, in the shadow pass.
};

struct FrameData {
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float3 cameraPosition;
    float fogDensity;
    float3 fogColor;
    uint lightCount;
    float3 ambient;
    uint lightBuffer;
    uint shadowBuffer;
    uint shadowAtlas;  // Bindless index of the depth texture that holds every shadow map.
    float shadowTexel; // One texel of the atlas, in texture coordinates.
    float normalStrength; // Multiplies every normal map's tilt.
    float3 ambientBelow;  // Ambient light from below; ambient comes from above.
    uint occlusionTexture; // Half-resolution ambient occlusion, or kNoTexture.
    float2 screenTexel;    // One pixel of the frame, in texture coordinates.
    float nearPlane;       // Of the camera: with reverse-Z at infinity, view depth is nearPlane / depth.
    uint padding;
};

// A point light has a cosOuter of -2, which every direction passes.
struct LightData {
    float3 position;
    float range;
    float3 direction;
    float fullRange; // Meters lit at full strength.
    float3 color;
    float cosOuter;
    float cosInner;
    uint shadow; // Index into the shadow buffer, or kNoShadow.
    uint2 padding;
};

static const uint kNoShadow = 0xFFFFFFFF;

// A shadow map: the light's view and projection, its tile in the atlas, and how far a receiver is pushed
// along its normal per meter from the light, against shadow acne.
struct ShadowData {
    float4x4 viewProjection;
    float2 atlasOffset;
    float atlasScale;
    float normalOffset;
};

static const uint kNoJoints = 0xFFFFFFFF;

struct InstanceData {
    float4x4 world;
    uint skinBuffer;
    uint firstJoint; // kNoJoints for a rigid draw.
    uint2 padding;
};

// One per joint of the frame's skinned draws: its world matrix times its inverse bind matrix.
struct JointData {
    float4x4 matrix;
};

// Matches resources::SkinWeights: four 16-bit joint indices, then four weights. The weights are
// separate floats, so that the structure keeps the 24 bytes of its twin.
struct SkinVertex {
    uint joints01;
    uint joints23;
    float weight0;
    float weight1;
    float weight2;
    float weight3;
};

static const uint kNoTexture = 0xFFFFFFFF;

struct MaterialData {
    float4 baseColorFactor;
    float3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint baseColorTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
};

// Fullscreen passes declare their own push constants.
#ifndef DAI_NO_MESH_PUSH_CONSTANTS
[[vk::push_constant]] MeshPushConstants g_push;
#endif
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];
[[vk::binding(2, 0)]] StructuredBuffer<FrameData> g_frames[];
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> g_instances[];
[[vk::binding(2, 0)]] StructuredBuffer<MaterialData> g_materials[];
[[vk::binding(2, 0)]] StructuredBuffer<JointData> g_joints[];
[[vk::binding(2, 0)]] StructuredBuffer<SkinVertex> g_skins[];
[[vk::binding(2, 0)]] StructuredBuffer<LightData> g_lights[];
[[vk::binding(2, 0)]] StructuredBuffer<ShadowData> g_shadows[];
[[vk::binding(1, 0)]] SamplerComparisonState g_compareSamplers[4];

// Matches rhi::SamplerIndex.
static const uint kSamplerLinearClamp = 0;
static const uint kSamplerLinearRepeat = 1;
static const uint kSamplerNearestClamp = 2;
static const uint kSamplerShadowCompare = 3;

#ifndef DAI_NO_MESH_PUSH_CONSTANTS
// The joints of a skinned vertex, blended by their weights (ADR-0020).
float4x4 SkinMatrix(InstanceData instance, uint vertexId) {
    const SkinVertex skin = g_skins[instance.skinBuffer][vertexId];
    const uint first = instance.firstJoint;
    const uint joints = g_push.jointBuffer;
    return g_joints[joints][first + (skin.joints01 & 0xFFFF)].matrix * skin.weight0 +
           g_joints[joints][first + (skin.joints01 >> 16)].matrix * skin.weight1 +
           g_joints[joints][first + (skin.joints23 & 0xFFFF)].matrix * skin.weight2 +
           g_joints[joints][first + (skin.joints23 >> 16)].matrix * skin.weight3;
}

// Where a draw places its vertex: the instance's world matrix, or its joints for a skinned draw.
float4x4 WorldMatrix(InstanceData instance, uint vertexId) {
    // Not a ternary operator: HLSL would evaluate both sides, and read a skin that rigid draws lack.
    float4x4 world = instance.world;
    if (instance.firstJoint != kNoJoints) {
        world = SkinMatrix(instance, vertexId);
    }
    return world;
}

#endif

#endif
