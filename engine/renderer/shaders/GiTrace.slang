// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The indirect diffuse light of each pixel (ADR-0040, ADR-0045): GiRays() rays from the
// G-buffer's surface in random directions, more often near its normal, and the light that comes back along
// each (BouncedLight). Rays in that distribution, averaged, give the light per unit of diffuse color; one
// per pixel is noisy, which the denoiser filters.
// With g_gi.mode 1, a debug view: each pixel shows the light its own surface's cell holds.
// With g_gi.spacing 2 or 4, half the pixels trace, or a quarter, different ones each frame (TracedPixel),
// and GiFill.slang fills the others (ADR-0093). Alpha holds the pixel's distance along the view, which the
// fill's history compares, and 0 for the sky.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_storageImages[];

// Below this many frames of history, on its first, the denoiser can only blur a pixel's one ray, and its noise
// shows: it gets GiNewRays() rays (ADR-0121). Below 4 frames, a fifth of the pixels traced more while the
// camera turned, for little more gain.
static const float kShortHistory = 2.0;
// As the denoiser's own test of the same surface (DenoiseTemporal.slang).
static const float kHistoryDepthTolerance = 0.05;

// Whether the denoiser has little or no history of this pixel's surface: it was out of view last frame,
// hidden, or seen for only a few frames. Found as the denoiser finds it, through the motion vectors, at the
// nearest pixel.
bool ShortHistory(FrameData frame, int2 pixel, float3 position, float linearDepth) {
    if (g_gi.denoiserGuides == kNoTexture) {
        return false;
    }
    const float2 uv = (float2(pixel) + 0.5) * frame.screenTexel + g_textures[g_gi.motion].Load(int3(pixel, 0)).xy;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return true;
    }
    const int2 previous = min(int2(uv * float2(frame.previousRenderSize)), int2(frame.previousRenderSize) - 1);
    const float4 guide = g_textures[g_gi.denoiserGuides].Load(int3(previous, 0));
    const float expectedDepth = mul(frame.previousViewProjection, float4(position, 1.0)).w;
    const float depthError = min(abs(guide.x - expectedDepth), abs(guide.x - linearDepth));
    return guide.x <= 0.0 || depthError > kHistoryDepthTolerance * linearDepth || guide.w < kShortHistory;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_gi.frameBuffer][0];
    const int2 pixel = TracedPixel(id.xy);
    if (any(pixel >= int2(g_gi.width, g_gi.height))) {
        return;
    }
    const float depth = g_textures[g_gi.depth].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        g_storageImages[g_gi.output][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) * frame.screenTexel, depth);
    const float3 faceNormal = FaceNormalAt(frame, pixel, position);
    if (g_gi.mode == 1) {
        g_storageImages[g_gi.output][pixel] = float4(CachedRadiance(frame, position, faceNormal), 1.0);
        return;
    }
    const float3 normal = normalize(g_textures[g_gi.normals].Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    uint random = Pcg((uint(pixel.y) * g_gi.width + uint(pixel.x)) * 9781u + g_gi.seed * 26699u);
    const uint rays =
        GiNewRays() > GiRays() && ShortHistory(frame, pixel, position, LinearDepth(frame, depth)) ? GiNewRays() : GiRays();
    float3 light = float3(0.0, 0.0, 0.0);
    for (uint index = 0; index < rays; ++index) {
        light += TraceBounce(frame, PixelBounceRay(frame, position, faceNormal, normal, random), random);
    }
    g_storageImages[g_gi.output][pixel] = float4(light / float(rays), LinearDepth(frame, depth));
}
