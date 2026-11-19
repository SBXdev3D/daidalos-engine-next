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

bool HoldsSample(int2 pixel) {
    return IsTraced(pixel);
}

#include "GiFill.slangh"

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

    float4 light = HistoryLight(frame, position);
    if (light.a <= 0.0) {
        const float3 faceNormal = FaceNormalAt(frame, pixel, position);
        light = NeighboursLight(frame, g_gi.output, pixel, size, position, faceNormal, viewDistance);
    }
    light.a = viewDistance;
    g_storageImages[g_gi.output][pixel] = light;
    g_storageImages[g_gi.history][pixel] = light;
}
