// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#ifndef DAIDALOS_MESH_TYPES_HLSLI
#define DAIDALOS_MESH_TYPES_HLSLI

// Each structure matches its C++ twin in engine/renderer/src/MeshPass.cpp.

// Indices only: everything else is in storage buffers, which the lighting data of Phase 7 will join.
struct MeshPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint instanceIndex;
    uint materialIndex;
};

struct FrameData {
    float4x4 viewProjection;
    float3 cameraPosition;
    float fogDensity;
    float3 fogColor;
    float padding;
    float3 spotPosition;
    float spotRange; // 0 without a light.
    float3 spotDirection;
    float spotCosInner;
    float3 spotColor;
    float spotCosOuter;
};

struct InstanceData {
    float4x4 world;
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

// Matches rhi::SamplerIndex.
static const uint kSamplerLinearRepeat = 1;

#endif
