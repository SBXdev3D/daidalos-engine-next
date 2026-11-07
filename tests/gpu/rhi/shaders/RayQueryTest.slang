// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Casts one ray per thread straight down -Z, from (x, y, 10) with x and y read from a buffer, and writes
// what it hit: the instance's custom index plus one and the distance, or zero for a miss.

struct Push {
    uint origins;
    uint results;
    uint structure;
    uint count;
    uint mask;
};

[[vk::push_constant]] Push g_push;
[[vk::binding(2, 0)]] StructuredBuffer<float2> g_origins[];
[[vk::binding(2, 0)]] RWStructuredBuffer<float2> g_results[];
[[vk::binding(3, 0)]] RaytracingAccelerationStructure g_accelerationStructures[];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_push.count) {
        return;
    }
    RayDesc ray;
    ray.Origin = float3(g_origins[g_push.origins][id.x], 10.0);
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin = 0.0;
    ray.TMax = 100.0;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_accelerationStructures[g_push.structure], RAY_FLAG_NONE, g_push.mask, ray);
    while (query.Proceed()) {
        // Instances that are not opaque stop here: accept their candidate as an alpha test would.
        query.CommitNonOpaqueTriangleHit();
    }
    float2 result = float2(0.0, 0.0);
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        result = float2(float(query.CommittedInstanceID() + 1), query.CommittedRayT());
    }
    g_results[g_push.results][id.x] = result;
}
