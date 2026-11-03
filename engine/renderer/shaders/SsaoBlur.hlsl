// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Smooths the raw ambient occlusion over 5x5 half-resolution pixels (ADR-0024), without blurring across
// edges: a neighbor counts less the farther its depth is from the pixel's.

#include "AmbientOcclusion.hlsli"

float ViewDepth(FrameData frame, float2 uv) {
    const float depth = g_textures[g_ao.depthTexture].SampleLevel(g_samplers[kSamplerNearestClamp], uv, 0.0).r;
    return depth > 0.0 ? frame.nearPlane / depth : 1.0e6;
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_ao.frameBuffer][0];
    const float2 uv = position.xy * g_ao.sourceTexel;
    const float centerDepth = ViewDepth(frame, uv);
    float sum = 0.0;
    float weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const float2 neighbor = uv + float2(x, y) * g_ao.sourceTexel;
            const float weight = exp(-abs(ViewDepth(frame, neighbor) - centerDepth) / (0.05 * centerDepth + 0.01));
            sum += g_textures[g_ao.sourceTexture].SampleLevel(g_samplers[kSamplerNearestClamp], neighbor, 0.0).r * weight;
            weights += weight;
        }
    }
    const float open = sum / max(weights, 0.0001);
    return float4(open, open, open, 1.0);
}
