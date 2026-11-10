// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Temporal antialiasing (ADR-0037): blends each jittered frame with the history of the earlier ones,
// found where the motion vectors say each pixel was. The history is clipped towards the colors around
// the pixel, so that what is no longer there fades instead of trailing.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct ResolvePushConstants {
    uint frameBuffer;
    uint color;    // The frame's HDR color, jittered.
    uint depth;    // Its depth, reverse-Z: 0 where nothing was drawn.
    uint motion;   // The pre-pass's motion vectors, in texture coordinates.
    uint reactive; // How little to trust the history, from the particles' coverage.
    uint history;  // The previous output, sampled.
    uint output;   // The new history, a storage image.
    uint reset;    // 1 when there is no history to use.
    uint2 extent;
};

[[vk::push_constant]] ResolvePushConstants g_resolve;
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];

// How much of the new frame each pixel takes: the history holds about ten frames.
static const float kCurrentWeight = 0.1;

// A reversible tone map: colors are blended and clipped in its range, so that one very bright sample
// does not dominate the average and flicker (Karis, "High Quality Temporal Supersampling", 2014).
float3 Compress(float3 color) {
    return color / (1.0 + max(color.r, max(color.g, color.b)));
}

float3 Expand(float3 color) {
    return color / max(1.0 - max(color.r, max(color.g, color.b)), 0.0001);
}

// Luma and two chroma axes: the neighbourhood's box is tighter around luma, which varies most.
float3 ToYCoCg(float3 color) {
    return float3(dot(color, float3(0.25, 0.5, 0.25)), dot(color, float3(0.5, 0.0, -0.5)),
                  dot(color, float3(-0.25, 0.5, -0.25)));
}

float3 FromYCoCg(float3 color) {
    return float3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

int2 ClampPixel(int2 pixel) {
    return clamp(pixel, int2(0, 0), int2(g_resolve.extent) - 1);
}

float3 LoadColor(int2 pixel) {
    const float3 color = g_textures[g_resolve.color].Load(int3(ClampPixel(pixel), 0)).rgb;
    return ToYCoCg(Compress(max(color, 0.0)));
}

float3 SampleHistoryAt(float2 uv) {
    return g_textures[g_resolve.history].SampleLevel(g_samplers[kSamplerLinearClamp], uv, 0.0).rgb;
}

// The history with a Catmull-Rom filter, from five bilinear samples: sharper than bilinear, which would
// blur the image a little more every frame (Jimenez, "Filmic SMAA", 2016).
float3 SampleHistory(float2 uv) {
    const float2 size = float2(g_resolve.extent);
    const float2 position = uv * size;
    const float2 center = floor(position - 0.5) + 0.5;
    const float2 f = position - center;
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2;
    const float2 middle = (center + w2 / w12) / size;
    const float2 before = (center - 1.0) / size;
    const float2 after = (center + 2.0) / size;
    const float4 weights = float4(w12.x * w0.y, w0.x * w12.y, w12.x * w12.y, w3.x * w12.y);
    const float last = w12.x * w3.y;
    const float3 sum = SampleHistoryAt(float2(middle.x, before.y)) * weights.x +
                       SampleHistoryAt(float2(before.x, middle.y)) * weights.y + SampleHistoryAt(middle) * weights.z +
                       SampleHistoryAt(float2(after.x, middle.y)) * weights.w +
                       SampleHistoryAt(float2(middle.x, after.y)) * last;
    return max(sum / (weights.x + weights.y + weights.z + weights.w + last), 0.0);
}

// Moves the history towards the mean of the neighbourhood until it is inside the box of its standard
// deviations (variance clipping, Salvi 2016): tighter than the colors' minimum and maximum.
float3 ClipHistory(float3 history, float3 mean, float3 deviation) {
    const float3 offset = history - mean;
    const float3 units = abs(offset / max(deviation, 0.00001));
    const float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0 ? mean + offset / largest : history;
}

// The sky is infinitely far: only the camera's rotation moves it.
float2 SkyMotion(FrameData frame, float2 uv) {
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    const float4 direction = float4(through.xyz / through.w - frame.cameraPosition, 0.0);
    return ClipToUv(mul(frame.previousViewProjection, direction)) -
           ClipToUv(mul(frame.unjitteredViewProjection, direction));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_resolve.extent)) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const FrameData frame = g_frames[g_resolve.frameBuffer][0];

    // The colors around the pixel, and the nearest surface among them, whose motion the pixel takes:
    // at the edge of a moving object, the object's motion wins over the background's.
    const float3 current = LoadColor(pixel);
    float3 sum = 0.0;
    float3 squares = 0.0;
    float nearest = -1.0;
    int2 nearestPixel = pixel;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 neighbour = ClampPixel(pixel + int2(x, y));
            const float3 color = LoadColor(neighbour);
            sum += color;
            squares += color * color;
            const float depth = g_textures[g_resolve.depth].Load(int3(neighbour, 0)).r;
            if (depth > nearest) {
                nearest = depth;
                nearestPixel = neighbour;
            }
        }
    }
    const float3 mean = sum / 9.0;
    const float3 deviation = sqrt(max(squares / 9.0 - mean * mean, 0.0));

    const float2 uv = (float2(pixel) + 0.5) / float2(g_resolve.extent);
    const float2 motion = nearest > 0.0 ? g_textures[g_resolve.motion].Load(int3(nearestPixel, 0)).xy
                                        : SkyMotion(frame, uv);
    const float2 historyUv = uv + motion;
    const bool hasHistory = g_resolve.reset == 0 && all(historyUv >= 0.0) && all(historyUv <= 1.0);

    float3 resolved = current;
    if (hasHistory) {
        const float3 history = ClipHistory(ToYCoCg(Compress(SampleHistory(historyUv))), mean, deviation);
        const float reactive = g_textures[g_resolve.reactive].Load(int3(pixel, 0)).r;
        resolved = lerp(history, current, lerp(kCurrentWeight, 1.0, reactive));
    }
    g_storageImages[g_resolve.output][pixel] = float4(Expand(max(FromYCoCg(resolved), 0.0)), 1.0);
}
