// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Blends the light the update paths added to each cell this frame into the light the cell holds
// (ADR-0040). Each sample counts as much as each of the samples before it, but the older ones weigh less
// every frame, whether paths went through the cell or not: the cache forgets in about kMemory frames, so
// that it follows lights, doors and the flashlight. A cell that no path went through for kMaxAge frames is freed. With g_gi.reset, every cell is.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

static const float kMemory = 8.0;
// A cell worth more samples changes too slowly.
static const float kMaxSamples = 512.0;
static const uint kMaxAge = 120;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint cell = id.x;
    if (cell >= kCacheCapacity) {
        return;
    }
    const uint base = cell * 4;
    if (g_gi.reset != 0) {
        g_cacheWords[g_gi.keys][cell] = 0;
        g_cacheWords[g_gi.ages][cell] = 0;
        g_cacheRadiance[g_gi.radiance][cell] = float4(0.0, 0.0, 0.0, 0.0);
        g_cacheRadiance[g_gi.anchors][cell] = float4(0.0, 0.0, 0.0, 0.0);
        for (uint word = 0; word < 4; ++word) {
            g_cacheWords[g_gi.accumulation][base + word] = 0;
        }
        return;
    }
    if (g_cacheWords[g_gi.keys][cell] == 0) {
        return;
    }
    const uint samples = g_cacheWords[g_gi.accumulation][base + 3];
    if (samples == 0) {
        const uint age = g_cacheWords[g_gi.ages][cell] + 1;
        if (age > kMaxAge) {
            g_cacheWords[g_gi.keys][cell] = 0;
            g_cacheRadiance[g_gi.radiance][cell] = float4(0.0, 0.0, 0.0, 0.0);
            g_cacheRadiance[g_gi.anchors][cell] = float4(0.0, 0.0, 0.0, 0.0);
            g_cacheWords[g_gi.ages][cell] = 0;
        } else {
            // Its light is worth less every frame, so that the next samples count for more.
            g_cacheRadiance[g_gi.radiance][cell].a *= 1.0 - 1.0 / kMemory;
            g_cacheWords[g_gi.ages][cell] = age;
        }
        return;
    }
    const float3 sum = float3(g_cacheWords[g_gi.accumulation][base], g_cacheWords[g_gi.accumulation][base + 1],
                              g_cacheWords[g_gi.accumulation][base + 2]) /
                       kFixedPoint;
    for (uint word = 0; word < 4; ++word) {
        g_cacheWords[g_gi.accumulation][base + word] = 0;
    }
    const float4 stored = g_cacheRadiance[g_gi.radiance][cell];
    const float kept = stored.a * (1.0 - 1.0 / kMemory);
    const float weight = kept + float(samples);
    const float3 radiance = (stored.rgb * kept + sum) / weight;
    g_cacheRadiance[g_gi.radiance][cell] = float4(radiance, min(weight, kMaxSamples));
    g_cacheWords[g_gi.ages][cell] = 0;
}
