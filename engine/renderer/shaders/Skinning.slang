// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Skins the vertices of one skinned draw into world space, once per frame (ADR-0033). Rasterization and
// ray tracing both use the result, so they see the same surface. Their positions with the previous
// frame's joints go to a second buffer, for motion vectors (ADR-0037).

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct SkinningPushConstants {
    uint source;      // The mesh's vertices.
    uint skin;        // Its joints and weights, one per vertex.
    uint joints;      // The frame's joint matrices.
    uint firstJoint;  // Of the draw.
    uint target;      // The skinned vertices.
    uint firstVertex; // Of the draw, in target.
    uint vertexCount;
    uint previousJoints; // Added to a joint's index: the same joint in the previous frame.
    uint previousTarget; // The previous positions, three floats each.
};

[[vk::push_constant]] SkinningPushConstants g_skinning;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> g_skinnedFloats[];

// The joints of a vertex, blended by their weights (ADR-0020), from the frame's joints or, after
// previousJoints, the previous frame's.
float4x4 SkinMatrix(uint vertex, uint first) {
    const SkinVertex skin = g_skins[g_skinning.skin][vertex];
    const uint joints = g_skinning.joints;
    return g_joints[joints][first + (skin.joints01 & 0xFFFF)].matrix * skin.weight0 +
           g_joints[joints][first + (skin.joints01 >> 16)].matrix * skin.weight1 +
           g_joints[joints][first + (skin.joints23 & 0xFFFF)].matrix * skin.weight2 +
           g_joints[joints][first + (skin.joints23 >> 16)].matrix * skin.weight3;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint vertex = id.x;
    if (vertex >= g_skinning.vertexCount) {
        return;
    }
    float source[kVertexFloats];
    for (uint index = 0; index < kVertexFloats; ++index) {
        source[index] = g_vertexFloats[g_skinning.source][vertex * kVertexFloats + index];
    }
    const float4x4 world = SkinMatrix(vertex, g_skinning.firstJoint);
    const float3 position = mul(world, float4(source[0], source[1], source[2], 1.0)).xyz;
    const float3 normal = TransformNormal(world, float3(source[3], source[4], source[5]));
    // A tangent lies in the surface, so it follows the matrix itself.
    const float3 tangent = mul((float3x3)world, float3(source[6], source[7], source[8]));

    const float skinned[kVertexFloats] = {position.x, position.y, position.z, normal.x,  normal.y,  normal.z,
                                          tangent.x,  tangent.y,  tangent.z,  source[9], source[10], source[11]};
    const uint first = (g_skinning.firstVertex + vertex) * kVertexFloats;
    for (uint index = 0; index < kVertexFloats; ++index) {
        g_skinnedFloats[g_skinning.target][first + index] = skinned[index];
    }

    const float4x4 previousWorld = SkinMatrix(vertex, g_skinning.firstJoint + g_skinning.previousJoints);
    const float3 previous = mul(previousWorld, float4(source[0], source[1], source[2], 1.0)).xyz;
    const uint previousFirst = (g_skinning.firstVertex + vertex) * 3;
    g_skinnedFloats[g_skinning.previousTarget][previousFirst] = previous.x;
    g_skinnedFloats[g_skinning.previousTarget][previousFirst + 1] = previous.y;
    g_skinnedFloats[g_skinning.previousTarget][previousFirst + 2] = previous.z;
}
