// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Fills the pixels that traced no indirect light this frame (ADR-0093). GiTrace.slang traced half the pixels,
// in a checkerboard, or one of each two by two (TracedPixel), and each of the others takes the light its own
// surface had last frame: from the history, where last frame's view saw the same point, at the same distance.
// The history holds a sample per pixel, traced then or brought forward the same way, so that the noise stays
// a pixel's own, which the denoiser filters as it filters every traced pixel's. A neighbour's light, spread
// over the pixels around it, would be noise the size of several pixels, which the denoiser takes for detail.
// Only where the history did not see the point, a surface that comes into view or that moves, the pixel takes
// the light of the traced pixels next to it on its surface. Every pixel then goes into the history.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_storageImages[];

// How far from its distance in the history a point may be, relative to it, and still be the same point.
static const float kHistoryTolerance = 0.02;
// How far from a pixel's plane a neighbour may lie, per meter from the camera, before its weight falls by e.
static const float kPlaneTolerance = 0.02;
// How sharply the weight falls as the neighbour's normal turns away: normal maps differ from pixel to pixel.
static const float kNormalPower = 2.0;

// The light of the traced pixels next to pixel, on its surface: the four nearest, across and down in a
// checkerboard, diagonally or two apart at a quarter.
float3 NeighboursLight(FrameData frame, int2 pixel, int2 size, float3 position, float3 faceNormal, float viewDistance) {
    const float3 normal = normalize(g_textures[g_gi.normals].Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    const float tolerance = kPlaneTolerance * max(viewDistance, 0.1);
    float3 light = float3(0.0, 0.0, 0.0);
    float weights = 0.0;
    float3 nearest = float3(0.0, 0.0, 0.0);
    int nearestDistance = 1000;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int2 traced = pixel + int2(x, y);
            const int distanceSquared = x * x + y * y;
            if (distanceSquared > 4 || any(traced < int2(0, 0)) || any(traced >= size) || !IsTraced(traced)) {
                continue;
            }
            const float4 sample = g_storageImages[g_gi.output][traced];
            if (sample.a <= 0.0) {
                continue; // The sky.
            }
            const float neighbourDepth = g_textures[g_gi.depth].Load(int3(traced, 0)).r;
            const float3 neighbour =
                WorldPosition(frame, (float2(traced) + 0.5) * frame.screenTexel, neighbourDepth);
            const float offPlane = abs(dot(faceNormal, neighbour - position)) / tolerance;
            if (distanceSquared < nearestDistance && offPlane < 1.0) {
                nearestDistance = distanceSquared;
                nearest = sample.rgb;
            }
            const float3 neighbourNormal =
                normalize(g_textures[g_gi.normals].Load(int3(traced, 0)).xyz * 2.0 - 1.0);
            const float facing = pow(saturate(dot(normal, neighbourNormal)), kNormalPower);
            const float weight = facing * exp(-offPlane - float(distanceSquared) * 0.5);
            light += sample.rgb * weight;
            weights += weight;
        }
    }
    return weights > 1e-3 ? light / weights : nearest;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_gi.frameBuffer][0];
    const int2 pixel = int2(id.xy);
    const int2 size = int2(g_gi.width, g_gi.height);
    if (any(pixel >= size)) {
        return;
    }
    if (IsTraced(pixel)) {
        g_storageImages[g_gi.history][pixel] = g_storageImages[g_gi.output][pixel];
        return;
    }
    const float depth = g_textures[g_gi.depth].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        g_storageImages[g_gi.output][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_storageImages[g_gi.history][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) * frame.screenTexel, depth);
    const float viewDistance = LinearDepth(frame, depth);

    float4 light = float4(0.0, 0.0, 0.0, -1.0);
    if (g_gi.historyValid != 0) {
        // Last frame drew a corner of its own size (ADR-0049).
        const float4 clip = mul(frame.previousViewProjection, float4(position, 1.0));
        const int2 previous = int2(floor(ClipToUv(clip) * float2(frame.previousRenderSize)));
        if (clip.w > 0.0 && all(previous >= int2(0, 0)) && all(previous < int2(frame.previousRenderSize))) {
            const float4 history = g_storageImages[g_gi.previousHistory][previous];
            if (history.a > 0.0 && abs(history.a - clip.w) < kHistoryTolerance * clip.w) {
                light = float4(history.rgb, viewDistance);
            }
        }
    }
    if (light.a < 0.0) {
        const float3 faceNormal = FaceNormalAt(frame, pixel, position);
        light = float4(NeighboursLight(frame, pixel, size, position, faceNormal, viewDistance), viewDistance);
    }
    g_storageImages[g_gi.output][pixel] = light;
    g_storageImages[g_gi.history][pixel] = light;
}
