// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The histogram of a frame's luminance, in log2 nits, which auto-exposure meters (ADR-0048). Pixels near the
// center weigh more: the player looks there, and the flashlight points there. Each group adds up its pixels
// in shared memory, then adds its bins to the histogram. With clear set, one group zeroes the histogram.
// Exposure.hpp holds the bins' layout.

struct HistogramPushConstants {
    uint image;     // An HDR image, exposed.
    uint histogram; // kBins uints.
    uint width;
    uint height;
    float toNits;   // What undoes the exposure.
    uint clear;
};

[[vk::push_constant]] HistogramPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> g_histograms[];

static const uint kBins = 128;
static const float kLowest = -10.0;
static const float kHighest = 16.0;
// Of the screen's height: the center weighs most, the corners still count.
static const float kCenterSpread = 0.35;
// Weights are counted in fixed point.
static const float kWeightScale = 64.0;

groupshared uint g_bins[kBins];

uint Bin(float luminance) {
    if (luminance <= exp2(kLowest)) {
        return 0;
    }
    const float position = (log2(luminance) - kLowest) / (kHighest - kLowest);
    return 1 + min(uint(position * float(kBins - 1)), kBins - 2);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex) {
    if (g_push.clear != 0) {
        if (index < kBins) {
            g_histograms[g_push.histogram][index] = 0;
        }
        return;
    }
    if (index < kBins) {
        g_bins[index] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x < g_push.width && id.y < g_push.height) {
        const float3 color = max(g_textures[g_push.image].Load(int3(id.xy, 0)).rgb, 0.0) * g_push.toNits;
        const float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        // Distances in heights of the screen, from its center.
        const float2 offset = (float2(id.xy) + 0.5 - 0.5 * float2(g_push.width, g_push.height)) / float(g_push.height);
        const float weight = exp(-dot(offset, offset) / (2.0 * kCenterSpread * kCenterSpread));
        InterlockedAdd(g_bins[Bin(luminance)], max(uint(weight * kWeightScale + 0.5), 1u));
    }
    GroupMemoryBarrierWithGroupSync();

    if (index < kBins && g_bins[index] != 0) {
        InterlockedAdd(g_histograms[g_push.histogram][index], g_bins[index]);
    }
}
