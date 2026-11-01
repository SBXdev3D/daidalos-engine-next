// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Forward shading of opaque meshes with fixed lighting, until lights exist (Phase 7). Materials hold
// the full metallic-roughness data, but only the base color is shaded for now.

#include "MeshTypes.hlsli"

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD;
};

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD;
    [[vk::location(2)]] float3 worldPosition : POSITION;
};

// Transforms a normal with the cofactor matrix, which stays correct under non-uniform scale and
// needs no inverse.
float3 TransformNormal(float4x4 world, float3 normal) {
    const float3 x = float3(world[0][0], world[1][0], world[2][0]);
    const float3 y = float3(world[0][1], world[1][1], world[2][1]);
    const float3 z = float3(world[0][2], world[1][2], world[2][2]);
    return cross(y, z) * normal.x + cross(z, x) * normal.y + cross(x, y) * normal.z;
}

// The light of the frame's spot at a surface, with a soft edge and no sudden end to its range.
float3 SpotLighting(FrameData frame, float3 position, float3 normal) {
    if (frame.spotRange <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 toLight = frame.spotPosition - position;
    const float distanceToLight = length(toLight);
    const float3 direction = toLight / max(distanceToLight, 0.001);
    const float cone = smoothstep(frame.spotCosOuter, frame.spotCosInner, dot(-direction, frame.spotDirection));
    const float reach = saturate(1.0 - distanceToLight * distanceToLight / (frame.spotRange * frame.spotRange));
    return frame.spotColor * cone * reach * reach * saturate(dot(normal, direction));
}

VertexOutput VSMain(VertexInput input) {
    const float4x4 viewProjection = g_frames[g_push.frameBuffer][0].viewProjection;
    const float4x4 world = g_instances[g_push.instanceBuffer][g_push.instanceIndex].world;

    VertexOutput output;
    const float4 worldPosition = mul(world, float4(input.position, 1.0));
    output.clipPosition = mul(viewProjection, worldPosition);
    output.worldNormal = TransformNormal(world, input.normal);
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

float4 PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];

    float4 baseColor = material.baseColorFactor;
    if (material.baseColorTexture != kNoTexture) {
        baseColor *= g_textures[material.baseColorTexture].Sample(g_samplers[kSamplerLinearRepeat], input.uv);
    }
    if (baseColor.a < material.alphaCutoff) {
        discard;
    }

    float3 normal = normalize(input.worldNormal);
    if (!isFrontFace) {
        normal = -normal; // The back of a double-sided material.
    }
    const float3 toLight = normalize(float3(0.4, 1.0, 0.6));
    const float diffuse = saturate(dot(normal, toLight));
    const float ambient = 0.2 + 0.1 * (normal.y * 0.5 + 0.5);
    const float3 lit = baseColor.rgb * (ambient + diffuse);

    const float visibility = exp(-frame.fogDensity * distance(input.worldPosition, frame.cameraPosition));
    const float3 spot = baseColor.rgb * SpotLighting(frame, input.worldPosition, normal);
    return float4(lerp(frame.fogColor, lit, visibility) + spot, 1.0);
}
