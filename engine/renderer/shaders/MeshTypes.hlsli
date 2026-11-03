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
};

struct FrameData {
    float4x4 viewProjection;
    float3 cameraPosition;
    float fogDensity;
    float3 fogColor;
    uint lightCount;
    float3 ambient;
    uint lightBuffer;
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
    uint3 padding;
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

[[vk::push_constant]] MeshPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[3];
[[vk::binding(2, 0)]] StructuredBuffer<FrameData> g_frames[];
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> g_instances[];
[[vk::binding(2, 0)]] StructuredBuffer<MaterialData> g_materials[];
[[vk::binding(2, 0)]] StructuredBuffer<JointData> g_joints[];
[[vk::binding(2, 0)]] StructuredBuffer<SkinVertex> g_skins[];
[[vk::binding(2, 0)]] StructuredBuffer<LightData> g_lights[];

// Matches rhi::SamplerIndex.
static const uint kSamplerLinearRepeat = 1;

#endif
