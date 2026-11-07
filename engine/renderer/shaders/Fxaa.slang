// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Fast approximate antialiasing (ADR-0026), after the algorithm of FXAA 3.11 by Timothy Lottes: where
// the luma of a pixel contrasts with its neighbors, the edge's direction is found, it is followed both
// ways to its ends, and the pixel is moved across the edge by how far it is from the nearer end. Thin
// details also blend with their surroundings. The source holds tonemapped colors, sRGB-encoded, with their
// luma in alpha; the target is an sRGB image, which encodes the linear result.

struct FxaaPushConstants {
    uint sourceTexture;
    uint padding;
    float2 texel; // One pixel of the source, in texture coordinates.
};

[[vk::push_constant]] FxaaPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];

static const uint kSamplerLinearClamp = 0;
// Contrast below which a pixel is left alone: in dark areas, and relative to the brightest neighbor.
static const float kEdgeThresholdMin = 0.0312;
static const float kEdgeThreshold = 0.125;
// How much thin details blend: 0 keeps them sharp, 1 softens them.
static const float kSubpixel = 0.75;
// Steps of the search along the edge, in pixels: short at first, longer far from the pixel.
static const uint kSearchSteps = 12;
static const float kStepSizes[kSearchSteps] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 Fetch(float2 uv) {
    return g_textures[g_push.sourceTexture].SampleLevel(g_samplers[kSamplerLinearClamp], uv, 0.0);
}

float Luma(float2 uv, float2 offset) {
    return Fetch(uv + offset * g_push.texel).a;
}

float3 SrgbToLinear(float3 color) {
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

float3 Antialias(float2 uv) {
    const float4 center = Fetch(uv);
    const float lumaM = center.a;
    const float lumaN = Luma(uv, float2(0, -1));
    const float lumaS = Luma(uv, float2(0, 1));
    const float lumaE = Luma(uv, float2(1, 0));
    const float lumaW = Luma(uv, float2(-1, 0));
    const float highest = max(max(max(lumaN, lumaS), max(lumaE, lumaW)), lumaM);
    const float lowest = min(min(min(lumaN, lumaS), min(lumaE, lumaW)), lumaM);
    const float range = highest - lowest;
    if (range < max(kEdgeThresholdMin, highest * kEdgeThreshold)) {
        return center.rgb;
    }

    const float lumaNW = Luma(uv, float2(-1, -1));
    const float lumaNE = Luma(uv, float2(1, -1));
    const float lumaSW = Luma(uv, float2(-1, 1));
    const float lumaSE = Luma(uv, float2(1, 1));

    // A horizontal edge changes most from north to south.
    const float edgeHorizontal = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaM) +
                                 abs(lumaNE + lumaSE - 2.0 * lumaE);
    const float edgeVertical = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaM) +
                               abs(lumaSW + lumaSE - 2.0 * lumaS);
    const bool horizontal = edgeHorizontal >= edgeVertical;

    // Which side of the pixel the edge lies on: where the luma changes the most.
    const float luma1 = horizontal ? lumaN : lumaW;
    const float luma2 = horizontal ? lumaS : lumaE;
    const float gradient1 = luma1 - lumaM;
    const float gradient2 = luma2 - lumaM;
    const bool side1 = abs(gradient1) >= abs(gradient2);
    const float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    float stepLength = horizontal ? g_push.texel.y : g_push.texel.x;
    float lumaLocal;
    if (side1) {
        stepLength = -stepLength;
        lumaLocal = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocal = 0.5 * (luma2 + lumaM);
    }

    // Halfway between the pixel and its neighbor across the edge, then along the edge both ways until the
    // luma leaves the edge's average.
    float2 edgeUv = uv;
    if (horizontal) {
        edgeUv.y += stepLength * 0.5;
    } else {
        edgeUv.x += stepLength * 0.5;
    }
    const float2 along = horizontal ? float2(g_push.texel.x, 0.0) : float2(0.0, g_push.texel.y);
    float2 uv1 = edgeUv - along;
    float2 uv2 = edgeUv + along;
    float lumaEnd1 = Fetch(uv1).a - lumaLocal;
    float lumaEnd2 = Fetch(uv2).a - lumaLocal;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    for (uint step = 1; step < kSearchSteps && !(reached1 && reached2); ++step) {
        if (!reached1) {
            uv1 -= along * kStepSizes[step];
            lumaEnd1 = Fetch(uv1).a - lumaLocal;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += along * kStepSizes[step];
            lumaEnd2 = Fetch(uv2).a - lumaLocal;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }

    // The nearer end decides: the pixel moves across the edge the more, the nearer it is to the end, and
    // only if the luma at that end varies the other way from the pixel's.
    const float distance1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    const float distance2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    const bool nearer1 = distance1 < distance2;
    const float nearest = min(distance1, distance2);
    const float pixelOffset = 0.5 - nearest / (distance1 + distance2);
    const bool centerSmaller = lumaM < lumaLocal;
    const bool correct = ((nearer1 ? lumaEnd1 : lumaEnd2) < 0.0) != centerSmaller;
    float offset = correct ? pixelOffset : 0.0;

    // Thin details: blend with the average of the neighbors, by how much the pixel stands out of them.
    const float lumaAverage = (2.0 * (lumaN + lumaS + lumaE + lumaW) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    const float subpixel = smoothstep(0.0, 1.0, saturate(abs(lumaAverage - lumaM) / range));
    offset = max(offset, subpixel * subpixel * kSubpixel);

    float2 finalUv = uv;
    if (horizontal) {
        finalUv.y += offset * stepLength;
    } else {
        finalUv.x += offset * stepLength;
    }
    return Fetch(finalUv).rgb;
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    return float4(SrgbToLinear(Antialias(position.xy * g_push.texel)), 1.0);
}
