// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Forward shading of opaque meshes with fixed lighting, until lights exist (Phase 6).

#include "MeshTypes.hlsli"

[[vk::push_constant]] MeshPushConstants g_push;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
};

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL;
    [[vk::location(1)]] float3 color : COLOR;
};

// Transforms a normal with the cofactor matrix, which stays correct under non-uniform scale and
// needs no inverse.
float3 TransformNormal(float4x4 world, float3 normal) {
    const float3 x = float3(world[0][0], world[1][0], world[2][0]);
    const float3 y = float3(world[0][1], world[1][1], world[2][1]);
    const float3 z = float3(world[0][2], world[1][2], world[2][2]);
    return cross(y, z) * normal.x + cross(z, x) * normal.y + cross(x, y) * normal.z;
}

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    const float4 worldPosition = mul(g_push.world, float4(input.position, 1.0));
    output.clipPosition = mul(g_push.viewProjection, worldPosition);
    output.worldNormal = TransformNormal(g_push.world, input.normal);
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
    const float3 normal = normalize(input.worldNormal);
    const float3 toLight = normalize(float3(0.4, 1.0, 0.6));
    const float diffuse = saturate(dot(normal, toLight));
    const float ambient = 0.2 + 0.1 * (normal.y * 0.5 + 0.5);
    return float4(input.color * (ambient + diffuse), 1.0);
}
