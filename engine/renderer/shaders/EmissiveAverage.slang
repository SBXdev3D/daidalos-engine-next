// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The emission of each triangle of an emissive mesh and material, once, when they are first drawn together
// (ADR-0044): the emissive factor times the emissive texture averaged over the triangle, from a few points
// spread over it, read from the mip whose texels are about as large as the share of the triangle each point
// stands for. Also the triangle's area, in the mesh's space. The CPU reads the result back and draws the
// triangles by power.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct EmissiveAveragePushConstants {
    uint vertexBuffer; // The mesh's vertices, kVertexFloats floats each.
    uint indexBuffer;
    uint materialBuffer;
    uint material;
    uint output; // Per triangle: its emission in nits, and its area.
    uint triangleCount;
};

[[vk::push_constant]] EmissiveAveragePushConstants g_average;
[[vk::binding(2, 0)]] StructuredBuffer<uint> g_indices[];
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> g_output[];

// Points per side of the grid spread over a triangle.
static const uint kGrid = 8;

float3 PositionOf(uint vertex) {
    const uint first = vertex * kVertexFloats;
    const StructuredBuffer<float> floats = g_vertexFloats[g_average.vertexBuffer];
    return float3(floats[first], floats[first + 1], floats[first + 2]);
}

float2 UvOf(uint vertex) {
    const uint first = vertex * kVertexFloats;
    const StructuredBuffer<float> floats = g_vertexFloats[g_average.vertexBuffer];
    return float2(floats[first + 10], floats[first + 11]);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint triangle = id.x;
    if (triangle >= g_average.triangleCount) {
        return;
    }
    const uint a = g_indices[g_average.indexBuffer][triangle * 3];
    const uint b = g_indices[g_average.indexBuffer][triangle * 3 + 1];
    const uint c = g_indices[g_average.indexBuffer][triangle * 3 + 2];
    const float area = 0.5 * length(cross(PositionOf(b) - PositionOf(a), PositionOf(c) - PositionOf(a)));
    const MaterialData material = g_materials[g_average.materialBuffer][g_average.material];
    float3 emission = material.emissiveFactor;
    if (material.emissiveTexture != kNoTexture) {
        const float2 uvA = UvOf(a);
        const float2 uvB = UvOf(b);
        const float2 uvC = UvOf(c);
        uint width;
        uint height;
        uint levels;
        g_textures[material.emissiveTexture].GetDimensions(0, width, height, levels);
        const float2 edgeB = (uvB - uvA) * float2(width, height);
        const float2 edgeC = (uvC - uvA) * float2(width, height);
        const float texels = 0.5 * abs(edgeB.x * edgeC.y - edgeB.y * edgeC.x);
        const float lod = clamp(0.5 * log2(max(texels / float(kGrid * kGrid), 1.0)), 0.0, float(levels - 1));
        // The triangle cut into kGrid * kGrid equal triangles, each read at its centroid: inside the triangle,
        // never on an edge, where a texture that repeats would show its other side.
        float3 sum = float3(0.0, 0.0, 0.0);
        for (uint row = 0; row < kGrid; ++row) {
            for (uint column = 0; column + row < kGrid; ++column) {
                const float2 corner = float2(column, row);
                // The triangle with its right angle at the corner, then the one turned over beside it.
                for (uint turned = 0; turned < (column + row + 1 < kGrid ? 2u : 1u); ++turned) {
                    const float2 u = (corner + (turned == 0 ? 1.0 / 3.0 : 2.0 / 3.0)) / float(kGrid);
                    const float2 uv = uvA + (uvB - uvA) * u.x + (uvC - uvA) * u.y;
                    sum += g_textures[material.emissiveTexture].SampleLevel(g_samplers[kSamplerLinearRepeat], uv, lod).rgb;
                }
            }
        }
        emission *= sum / float(kGrid * kGrid);
    }
    g_output[g_average.output][triangle] = float4(emission, area);
}
