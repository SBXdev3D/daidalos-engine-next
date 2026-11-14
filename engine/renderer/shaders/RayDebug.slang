// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The debug view of the frame's acceleration structure (renderer.debug_view rays, ADR-0033): one ray per
// pixel from the camera, each instance in its own color, shaded by the angle of the triangle it hits.
// What it shows must match what rasterization draws.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct RayDebugPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
};

[[vk::push_constant]] RayDebugPushConstants g_debug;
[[vk::binding(2, 0)]] StructuredBuffer<uint> g_indices[];
[[vk::binding(3, 0)]] RaytracingAccelerationStructure g_accelerationStructures[];

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float3 VertexPosition(InstanceData instance, uint index) {
    const uint first = (instance.firstVertex + index) * kVertexFloats;
    return float3(g_vertexFloats[instance.vertexBuffer][first], g_vertexFloats[instance.vertexBuffer][first + 1],
                  g_vertexFloats[instance.vertexBuffer][first + 2]);
}

float3 InstanceColor(uint instance) {
    return frac(float3(instance * 0.618034, instance * 0.371, instance * 0.193) + 0.2) * 0.8 + 0.2;
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_debug.frameBuffer][0];
    // Pixels count down from the top; clip-space Y points up (CONVENTIONS.md, section 5).
    const float2 uv = position.xy * frame.screenTexel;
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    RayDesc ray;
    ray.Origin = frame.cameraPosition;
    ray.Direction = normalize(through.xyz / through.w - frame.cameraPosition);
    ray.TMin = frame.nearPlane;
    ray.TMax = frame.farPlane;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_accelerationStructures[frame.accelerationStructure], RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return float4(frame.fogColor, 1.0);
    }
    const uint instanceIndex = query.CommittedInstanceID();
    const InstanceData instance = g_instances[g_debug.instanceBuffer][instanceIndex];
    const uint triangle = query.CommittedPrimitiveIndex() * 3;
    const float3 a = VertexPosition(instance, g_indices[instance.indexBuffer][triangle]);
    const float3 b = VertexPosition(instance, g_indices[instance.indexBuffer][triangle + 1]);
    const float3 c = VertexPosition(instance, g_indices[instance.indexBuffer][triangle + 2]);
    const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
    const float3 normal = normalize(mul((float3x3)objectToWorld, cross(b - a, c - a)));
    const float facing = abs(dot(normal, ray.Direction));
    return float4(InstanceColor(instanceIndex) * (0.25 + 0.75 * facing), 1.0);
}
