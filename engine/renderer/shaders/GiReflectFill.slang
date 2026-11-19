// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Fills the reflections of the rough pixels that traced none this frame (ADR-0096). On rough surfaces, whose
// reflection is a blur read from the radiance cache, GiReflect.slang traced half the pixels, in a checkerboard
// that takes turns (IsTraced); sharp reflections trace every pixel. Each rough pixel that did not trace takes
// the reflection its own surface had last frame, from the history, or else its traced neighbours', as the
// indirect light's fill does (ADR-0093). Every pixel then goes into the history.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

// Whether a pixel holds a reflection traced this frame: sharp ones always, rough ones on their turn, none
// rougher than the reflections' limit.
bool HoldsSample(int2 pixel) {
    const float roughness = g_textures[g_gi.normals].Load(int3(pixel, 0)).a;
    return roughness <= g_gi.maxRoughness && (roughness < kSharpRoughness || IsTraced(pixel));
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
    const float depth = g_textures[g_gi.depth].Load(int3(pixel, 0)).r;
    const float roughness = g_textures[g_gi.normals].Load(int3(pixel, 0)).a;
    if (depth <= 0.0 || roughness > g_gi.maxRoughness) {
        // No reflection ray: nothing to bring forward.
        g_storageImages[g_gi.history][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float viewDistance = LinearDepth(frame, depth);
    if (HoldsSample(pixel)) {
        g_storageImages[g_gi.history][pixel] = float4(g_storageImages[g_gi.reflections][pixel].rgb, viewDistance);
        return;
    }
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) * frame.screenTexel, depth);
    const float3 faceNormal = FaceNormalAt(frame, pixel, position);
    // The neighbours' hit distance, which the denoiser follows the reflection with, and their light where the
    // history did not see the point.
    const float4 neighbours = NeighboursLight(frame, g_gi.reflections, pixel, size, position, faceNormal, viewDistance);
    const float4 history = HistoryLight(frame, position);
    const float3 light = history.a > 0.0 ? history.rgb : neighbours.rgb;
    g_storageImages[g_gi.reflections][pixel] = float4(light, neighbours.a);
    g_storageImages[g_gi.history][pixel] = float4(light, viewDistance);
}
