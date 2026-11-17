// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The frame's emissive triangles, each frame (ADR-0044): the corners of each triangle of an emissive draw in
// world space, with its emission, which light sampling reads (Lights.slangh). An emissive draw that moves, or
// is skinned, stays a light.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

struct EmissiveTrianglesPushConstants {
    uint instanceBuffer;
    uint instance; // The draw.
    uint firstTriangle; // Where its triangles go among the frame's.
    uint triangleCount;
    uint emission; // Per triangle of its mesh: its emission in nits, and its area.
    uint output;
    uint twoSided;
    uint padding;
};

[[vk::push_constant]] EmissiveTrianglesPushConstants g_triangles;
[[vk::binding(2, 0)]] StructuredBuffer<float4> g_emissions[];
[[vk::binding(2, 0)]] RWStructuredBuffer<EmissiveTriangle> g_emissiveOutput[];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint triangle = id.x;
    if (triangle >= g_triangles.triangleCount) {
        return;
    }
    const InstanceData instance = g_instances[g_triangles.instanceBuffer][g_triangles.instance];
    const uint first = triangle * 3;
    EmissiveTriangle result;
    result.a = mul(instance.world, float4(LoadVertex(instance, g_indices[instance.indexBuffer][first]).position, 1.0)).xyz;
    result.b = mul(instance.world, float4(LoadVertex(instance, g_indices[instance.indexBuffer][first + 1]).position, 1.0)).xyz;
    result.c = mul(instance.world, float4(LoadVertex(instance, g_indices[instance.indexBuffer][first + 2]).position, 1.0)).xyz;
    result.twoSided = g_triangles.twoSided;
    result.emission = g_emissions[g_triangles.emission][triangle].rgb * instance.emissiveScale;
    result.padding0 = 0;
    result.padding1 = 0;
    result.padding2 = 0;
    g_emissiveOutput[g_triangles.output][g_triangles.firstTriangle + triangle] = result;
}
