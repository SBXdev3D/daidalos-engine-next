// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// What the camera's lens and shutter do to the antialiased image (ADR-0063), at the output's size, before
// bloom: depth of field, from the aperture and the focus distance, then motion blur, along the motion vectors
// over the time the shutter is open. Each is a pass of this shader.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

// Matches CameraEffectsPushConstants in renderer/src/CameraEffects.cpp.
struct CameraEffectsPushConstants {
    uint frameBuffer;
    uint color;  // Sampled: the image so far.
    uint depth;  // Sampled: the G-buffer's depth, at the render size, in the corner of its target.
    uint motion; // Sampled: the motion vectors, likewise.
    uint output; // Storage image.
    uint mode;   // kDepthOfField or kMotionBlur.
    uint width;
    uint height;
    float cocScale;      // Pixels of blur per unit of |depth - focus| / depth.
    float focusDistance; // Meters.
    float maxCoc;        // Pixels: the widest blur.
    float shutter;       // Fraction of the frame's time the shutter is open.
};

static const uint kDepthOfField = 0;
static const uint kMotionBlur = 1;

[[vk::push_constant]] CameraEffectsPushConstants g_push;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_images[];

// Samples on a disc, spread evenly (Vogel's spiral): their radius from 0 to 1.
static const uint kDiscSamples = 32;
static const float kGoldenAngle = 2.39996323;

float DepthAt(FrameData frame, float2 uv) {
    const int2 pixel = int2(FrameToTarget(frame, uv) * float2(frame.targetSize));
    return LinearDepth(frame, g_textures[g_push.depth].Load(int3(pixel, 0)).r);
}

// Pixels across the circle a point at this depth blurs into.
float CircleOfConfusion(float depth) {
    return min(g_push.cocScale * abs(depth - g_push.focusDistance) / max(depth, 1e-3), g_push.maxCoc);
}

float3 ColorAt(float2 uv) {
    return g_textures[g_push.color].SampleLevel(g_samplers[kSamplerLinearClamp], uv, 0.0).rgb;
}

float3 DepthOfField(FrameData frame, float2 uv, float2 texel) {
    const float center = CircleOfConfusion(DepthAt(frame, uv));
    const float3 own = ColorAt(uv);
    if (center < 0.5) {
        return own;
    }
    // A sample counts where its own blur reaches this pixel: a sharp point in front does not smear onto the
    // blur behind it.
    float3 sum = own;
    float weight = 1.0;
    for (uint i = 0; i < kDiscSamples; ++i) {
        const float radius = sqrt((float(i) + 0.5) / float(kDiscSamples)) * center;
        const float angle = float(i) * kGoldenAngle;
        const float2 offset = float2(cos(angle), sin(angle)) * radius;
        const float2 at = uv + offset * texel;
        const float reach = CircleOfConfusion(DepthAt(frame, at));
        const float w = saturate(reach - radius + 1.0);
        sum += ColorAt(at) * w;
        weight += w;
    }
    return sum / weight;
}

float3 MotionBlur(FrameData frame, float2 uv, float2 texel) {
    const int2 pixel = int2(FrameToTarget(frame, uv) * float2(frame.targetSize));
    // Where the pixel was last frame, in texture coordinates; the shutter keeps part of the way.
    float2 motion = g_textures[g_push.motion].Load(int3(pixel, 0)).xy * g_push.shutter;
    const float pixels = length(motion / texel);
    if (pixels < 0.5) {
        return ColorAt(uv);
    }
    // At most 32 pixels of blur.
    motion *= min(1.0, 32.0 / pixels);
    const uint samples = 12;
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < samples; ++i) {
        const float t = (float(i) + 0.5) / float(samples) - 0.5;
        sum += ColorAt(uv + motion * t);
    }
    return sum / float(samples);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_push.width || id.y >= g_push.height) {
        return;
    }
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const float2 texel = 1.0 / float2(g_push.width, g_push.height);
    const float2 uv = (float2(id.xy) + 0.5) * texel;
    const float3 color = g_push.mode == kDepthOfField ? DepthOfField(frame, uv, texel) : MotionBlur(frame, uv, texel);
    g_images[g_push.output][id.xy] = float4(color, 1.0);
}
