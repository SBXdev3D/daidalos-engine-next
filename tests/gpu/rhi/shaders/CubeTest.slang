// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Samples a cube along the six axes, +X, -X, +Y, -Y, +Z, -Z, into a buffer.

struct Push {
    uint cube;
    uint results;
};

[[vk::push_constant]] Push g_push;
[[vk::binding(1, 0)]] SamplerState g_samplers[4];
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> g_results[];
[[vk::binding(5, 0)]] TextureCube g_cubeTextures[];

static const float3 kAxes[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
                                float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};

[numthreads(6, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // Nearest, clamped: sampler 2 (rhi::SamplerIndex::NearestClamp).
    g_results[g_push.results][id.x] = g_cubeTextures[g_push.cube].SampleLevel(g_samplers[2], kAxes[id.x], 0.0);
}
