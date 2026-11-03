// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Screen-space ambient occlusion at half resolution (ADR-0024): how much of the hemisphere above a pixel
// is closed by nearby geometry, from the pre-pass's depth and normals. 1 is open, 0 fully occluded.

#include "AmbientOcclusion.hlsli"

static const uint kSamples = 16;
static const float kTwoPi = 6.2831853;

float SceneDepth(float2 uv) {
    return g_textures[g_ao.depthTexture].SampleLevel(g_samplers[kSamplerNearestClamp], uv, 0.0).r;
}

// The world position of a pixel of the frame, from its depth.
float3 WorldPosition(FrameData frame, float2 uv, float depth) {
    const float4 world = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    return world.xyz / world.w;
}

// A different angle for each pixel, which the blur then averages out (Jimenez's interleaved gradient noise).
float Noise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_ao.frameBuffer][0];
    // The pixel's center, in the full-resolution depth.
    const float2 uv = position.xy * 2.0 * g_ao.sourceTexel;
    const float depth = SceneDepth(uv);
    if (depth <= 0.0) {
        return float4(1.0, 1.0, 1.0, 1.0); // Nothing drawn there.
    }
    const float3 center = WorldPosition(frame, uv, depth);
    const float centerDepth = frame.nearPlane / depth;
    const float3 normal = normalize(
        g_textures[g_ao.normalTexture].SampleLevel(g_samplers[kSamplerNearestClamp], uv, 0.0).xyz * 2.0 - 1.0);

    // A basis around the normal, turned by the noise.
    const float angle = Noise(position.xy) * kTwoPi;
    const float3 helper = abs(normal.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangentA = normalize(cross(helper, normal));
    const float3 tangentB = cross(normal, tangentA);
    const float3 tangent = tangentA * cos(angle) + tangentB * sin(angle);
    const float3 bitangent = cross(normal, tangent);

    float occlusion = 0.0;
    for (uint index = 0; index < kSamples; ++index) {
        // Points spread over the hemisphere, more of them close to the center.
        const float z = 1.0 - (index + 0.5) / kSamples;
        const float ring = sqrt(1.0 - z * z);
        const float turn = index * 2.39996323; // The golden angle.
        const float scale = lerp(0.15, 1.0, pow((index + 1.0) / kSamples, 2.0));
        const float3 offset = (tangent * cos(turn) * ring + bitangent * sin(turn) * ring + normal * z) * scale;
        const float4 clip = mul(frame.viewProjection, float4(center + offset * g_ao.radius, 1.0));
        if (clip.w <= 0.0) {
            continue;
        }
        const float2 sampleUv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        const float sceneDepth = SceneDepth(sampleUv);
        if (sceneDepth <= 0.0) {
            continue;
        }
        // In front of the sample point, but not so far in front that it is another object.
        const float sceneViewDepth = frame.nearPlane / sceneDepth;
        const float closeEnough = saturate(g_ao.radius / max(abs(centerDepth - sceneViewDepth), 0.0001));
        occlusion += (sceneViewDepth < clip.w - 0.02) ? closeEnough : 0.0;
    }
    const float open = saturate(1.0 - occlusion / kSamples * g_ao.intensity);
    return float4(open, open, open, 1.0);
}
