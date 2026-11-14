// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Forms the image the display shows (ADR-0048), drawn as one triangle over the screen: the scene with its
// bloom, scaled from the GPU's fixed exposure to the eye's, through the display transform, with the
// interface layer over it, encoded for the swapchain: sRGB, which the format encodes, or HDR10.
// DisplayTransform.cpp holds the same functions for the CPU.

struct OutputPushConstants {
    uint scene;     // The frame's HDR color, sampled.
    uint bloom;     // Its bloom, at a lower resolution, or kNone.
    uint layer;     // The interface, premultiplied, or kNone.
    uint toneMapper; // 0 AgX, 1 PBR Neutral.
    float scale;     // From the GPU's exposure to the eye's, with the brightness setting.
    float bloomShare;
    float headroom;  // Peak over paper white: 1 in SDR.
    float paperWhite; // Nits of the interface's white and of 1 in the image, in HDR10.
    uint hdr10;
};

static const uint kNone = 0xFFFFFFFF;

[[vk::push_constant]] OutputPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];
static const uint kSamplerLinearClamp = 0;

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// A matrix given by its columns, as GLSL and glm write them, times a vector.
float3 Columns(float3 c0, float3 c1, float3 c2, float3 v) {
    return c0 * v.x + c1 * v.y + c2 * v.z;
}

float3 Rec709ToRec2020(float3 color) {
    return Columns(float3(0.6274, 0.0691, 0.0164), float3(0.3293, 0.9195, 0.0880), float3(0.0433, 0.0113, 0.8956),
                   color);
}

float3 Rec2020ToRec709(float3 color) {
    return Columns(float3(1.6605, -0.1246, -0.0182), float3(-0.5876, 1.1329, -0.1006),
                   float3(-0.0728, -0.0083, 1.1187), color);
}

// AgX's sigmoid, fitted by a polynomial (Wrensch, "Minimal AgX implementation", 2023).
float3 AgxContrast(float3 x) {
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// Blender's and Filament's AgX, in the form three.js gives it (MIT).
float3 AgX(float3 color) {
    const float minEv = -12.47393; // log2(2^-10 * 0.18): stops below middle grey.
    const float maxEv = 4.026069;  // log2(2^6.5 * 0.18): stops above it.
    color = Rec709ToRec2020(max(color, 0.0));
    color = Columns(float3(0.856627153315983, 0.137318972929847, 0.11189821299995),
                    float3(0.0951212405381588, 0.761241990602591, 0.0767994186031903),
                    float3(0.0482516061458583, 0.101439036467562, 0.811302368396859), color);
    color = clamp((log2(max(color, 1e-10)) - minEv) / (maxEv - minEv), 0.0, 1.0);
    color = Columns(float3(1.1271005818144368, -0.1413297634984383, -0.14132976349843826),
                    float3(-0.11060664309660323, 1.157823702216272, -0.11060664309660294),
                    float3(-0.016493938717834573, -0.016493938717834257, 1.2519364065950405), AgxContrast(color));
    // The sigmoid's output is display-encoded with a 2.2 power.
    color = pow(max(color, 0.0), 2.2);
    return clamp(Rec2020ToRec709(color), 0.0, 1.0);
}

// Khronos PBR Neutral: colors below 0.76 stay as they are, and brighter ones are compressed towards white
// without changing their hue (ADR-0022).
float3 PbrNeutral(float3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    color = max(color, 0.0);
    const float lowest = min(color.r, min(color.g, color.b));
    const float offset = lowest < 0.08 ? lowest - 6.25 * lowest * lowest : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

// The SDR image spread up to the headroom: the largest channel follows the inverse of a Reinhard curve.
float3 ExpandHighlights(float3 color, float headroom) {
    const float largest = max(color.r, max(color.g, color.b));
    if (headroom <= 1.0 || largest <= 0.0) {
        return color;
    }
    const float expanded = largest / (1.0 - min(largest, 1.0) * (1.0 - 1.0 / headroom));
    return color * (expanded / largest);
}

// SMPTE ST 2084: nits from 0 to 10,000 to a signal from 0 to 1.
float3 PqEncode(float3 nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    const float3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const int2 pixel = int2(position.xy);
    float3 color = max(g_textures[g_push.scene].Load(int3(pixel, 0)).rgb, 0.0);
    if (g_push.bloom != kNone) {
        uint width;
        uint height;
        g_textures[g_push.scene].GetDimensions(width, height);
        const float2 uv = position.xy / float2(width, height);
        const float3 bloom = g_textures[g_push.bloom].SampleLevel(g_samplers[kSamplerLinearClamp], uv, 0.0).rgb;
        color = lerp(color, max(bloom, 0.0), g_push.bloomShare);
    }
    color *= g_push.scale;
    color = g_push.toneMapper == 0 ? AgX(color) : PbrNeutral(color);
    color = ExpandHighlights(color, g_push.headroom);
    if (g_push.layer != kNone) {
        const float4 layer = g_textures[g_push.layer].Load(int3(pixel, 0));
        color = layer.rgb + color * (1.0 - layer.a);
    }
    if (g_push.hdr10 != 0) {
        return float4(PqEncode(max(Rec709ToRec2020(color), 0.0) * g_push.paperWhite), 1.0);
    }
    return float4(color, 1.0);
}
