// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Depth of the meshes seen from a light, into its tile of the shadow atlas (ADR-0023). Only depth is
// written; alpha-tested materials, such as grids, cut their holes in the shadow too.

#include "MeshTypes.slangh"

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD;
};

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD;
};

VertexOutput VSMain(VertexInput input) {
    const InstanceData instance = g_instances[g_push.instanceBuffer][g_push.instanceIndex];
    const ShadowData shadow = g_shadows[g_frames[g_push.frameBuffer][0].shadowBuffer][g_push.shadowIndex];
    VertexOutput output;
    output.clipPosition = mul(shadow.viewProjection, mul(instance.world, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

void PSMain(VertexOutput input) {
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];
    if (material.alphaCutoff <= 0.0) {
        return;
    }
    float alpha = material.baseColorFactor.a;
    if (material.baseColorTexture != kNoTexture) {
        alpha *= g_textures[material.baseColorTexture].Sample(g_samplers[kSamplerLinearRepeat], input.uv).a;
    }
    if (alpha < material.alphaCutoff) {
        discard;
    }
}
