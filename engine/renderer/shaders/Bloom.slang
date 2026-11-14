// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Bloom without threshold (ADR-0048; Jimenez, "Next Generation Post Processing in Call of Duty: Advanced
// Warfare", 2014). Each pixel writes one pixel of a level of the chain:
// - down: 13 bilinear taps of the level above, twice its size; the first level weighs groups of taps by
//   1 / (1 + luminance), Karis' average, so that one bright pixel does not flicker as a big square;
// - up: a 3 x 3 tent over the smaller level, added to this level's own downsampled image.

struct BloomPushConstants {
    uint source;  // The level read through the filter, sampled.
    uint add;     // Up: this level's downsampled image, sampled.
    uint output;  // Storage index of the level written.
    uint width;   // The level written.
    uint height;
    uint mode;    // 0 down, 1 down with Karis' average, 2 up.
    float scale;  // Multiplies what is written.
};

[[vk::push_constant]] BloomPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_storageImages[];

static const uint kSamplerLinearClamp = 0;
static const uint kDown = 0;
static const uint kDownKaris = 1;

float3 Tap(float2 uv, float2 texel, float2 offset) {
    return g_textures[g_push.source].SampleLevel(g_samplers[kSamplerLinearClamp], uv + offset * texel, 0.0).rgb;
}

// What a group of four taps weighs in Karis' average.
float KarisWeight(float3 color) {
    return 1.0 / (1.0 + dot(color, float3(0.2126, 0.7152, 0.0722)));
}

float3 Downsample(float2 uv, float2 texel, bool karis) {
    const float3 a = Tap(uv, texel, float2(-2.0, -2.0));
    const float3 b = Tap(uv, texel, float2(0.0, -2.0));
    const float3 c = Tap(uv, texel, float2(2.0, -2.0));
    const float3 d = Tap(uv, texel, float2(-2.0, 0.0));
    const float3 e = Tap(uv, texel, float2(0.0, 0.0));
    const float3 f = Tap(uv, texel, float2(2.0, 0.0));
    const float3 g = Tap(uv, texel, float2(-2.0, 2.0));
    const float3 h = Tap(uv, texel, float2(0.0, 2.0));
    const float3 i = Tap(uv, texel, float2(2.0, 2.0));
    const float3 j = Tap(uv, texel, float2(-1.0, -1.0));
    const float3 k = Tap(uv, texel, float2(1.0, -1.0));
    const float3 l = Tap(uv, texel, float2(-1.0, 1.0));
    const float3 m = Tap(uv, texel, float2(1.0, 1.0));
    // Five overlapping boxes of four taps: the center one weighs half, the corner ones an eighth each.
    const float3 groups[5] = {(j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                              (d + e + g + h) * 0.25, (e + f + h + i) * 0.25};
    const float weights[5] = {0.5, 0.125, 0.125, 0.125, 0.125};
    float3 sum = 0.0;
    float total = 0.0;
    for (int group = 0; group < 5; ++group) {
        const float weight = weights[group] * (karis ? KarisWeight(groups[group]) : 1.0);
        sum += groups[group] * weight;
        total += weight;
    }
    return sum / total;
}

float3 Upsample(float2 uv, float2 texel) {
    return (Tap(uv, texel, float2(-1.0, -1.0)) + Tap(uv, texel, float2(1.0, -1.0)) + Tap(uv, texel, float2(-1.0, 1.0)) +
            Tap(uv, texel, float2(1.0, 1.0)) +
            2.0 * (Tap(uv, texel, float2(0.0, -1.0)) + Tap(uv, texel, float2(-1.0, 0.0)) +
                   Tap(uv, texel, float2(1.0, 0.0)) + Tap(uv, texel, float2(0.0, 1.0))) +
            4.0 * Tap(uv, texel, float2(0.0, 0.0))) /
           16.0;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_push.width || id.y >= g_push.height) {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(g_push.width, g_push.height);
    uint sourceWidth;
    uint sourceHeight;
    g_textures[g_push.source].GetDimensions(sourceWidth, sourceHeight);
    const float2 texel = 1.0 / float2(sourceWidth, sourceHeight);
    float3 color;
    if (g_push.mode == kDown || g_push.mode == kDownKaris) {
        color = Downsample(uv, texel, g_push.mode == kDownKaris);
    } else {
        color = Upsample(uv, texel) + g_textures[g_push.add].Load(int3(id.xy, 0)).rgb;
    }
    g_storageImages[g_push.output][id.xy] = float4(max(color * g_push.scale, 0.0), 1.0);
}
