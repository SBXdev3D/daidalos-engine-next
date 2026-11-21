// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Forms the image the display shows (ADR-0048), drawn as one triangle over the screen: the scene, sharpened
// (ADR-0049), with its bloom, scaled from the GPU's fixed exposure to the eye's, through the display transform, with the
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
    float sharpness; // Of the contrast-adaptive sharpening, from 0 (none) to 1.
    int sceneX;      // Where the scene starts in the window (ADR-0055): black around it.
    int sceneY;
    float grain;      // The lens and film (ADR-0063), each from 0 (none) to 1.
    float vignette;
    float aberration;
    float lensDirt;   // Needs the bloom.
    uint frame;       // Moves the grain.
    uint colorVision; // 0 normal, 1 protanopia, 2 deuteranopia, 3 tritanopia (ADR-0075).
    uint padding1;
    uint padding2;
};

static const uint kNone = 0xFFFFFFFF;

[[vk::push_constant]] OutputPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];
static const uint kSamplerLinearClamp = 0;

// Corrects linear color for a kind of color blindness (ADR-0075): what the viewer would see is simulated
// (Machado, Oliveira and Fernandes 2009, full severity, in linear sRGB), and the difference they lose is moved
// into channels they tell apart (Fidaner, Lin and Ozguven's daltonization).
float3 Daltonize(float3 color, uint kind) {
    float3x3 seen;
    float3x3 shift;
    if (kind == 1) {
        seen = float3x3(0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998);
        shift = float3x3(0.0, 0.0, 0.0, 0.7, 1.0, 0.0, 0.7, 0.0, 1.0);
    } else if (kind == 2) {
        seen = float3x3(0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881);
        shift = float3x3(1.0, 0.7, 0.0, 0.0, 0.0, 0.0, 0.0, 0.7, 1.0);
    } else {
        seen = float3x3(1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900);
        shift = float3x3(1.0, 0.0, 0.7, 0.0, 1.0, 0.7, 0.0, 0.0, 0.0);
    }
    const float3 lost = color - mul(seen, color);
    return max(color + mul(shift, lost), 0.0);
}

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

// A reversible tone map, so that sharpening works on values from 0 to 1 whatever the light.
float3 Compress(float3 color) {
    return color / (1.0 + max(color.r, max(color.g, color.b)));
}

float3 Expand(float3 color) {
    return color / max(1.0 - max(color.r, max(color.g, color.b)), 0.0001);
}

float3 LoadScene(int2 pixel, int2 size) {
    return Compress(max(g_textures[g_push.scene].Load(int3(clamp(pixel, int2(0, 0), size - 1), 0)).rgb, 0.0));
}

// Robust contrast-adaptive sharpening, as FSR's RCAS does it (AMD, MIT): the pixel moves away from its four
// neighbours by as much as their range allows without leaving it, so that edges sharpen and nothing rings.
float3 Sharpen(int2 pixel, int2 size, float sharpness) {
    const float3 north = LoadScene(pixel - int2(0, 1), size);
    const float3 west = LoadScene(pixel - int2(1, 0), size);
    const float3 center = LoadScene(pixel, size);
    const float3 east = LoadScene(pixel + int2(1, 0), size);
    const float3 south = LoadScene(pixel + int2(0, 1), size);
    const float3 lowest = min(min(north, west), min(east, south));
    const float3 highest = max(max(north, west), max(east, south));
    // The largest negative lobe that keeps the result between 0 and 1, channel by channel.
    const float3 toLowest = lowest / (4.0 * max(highest, 0.0001));
    const float3 toHighest = (1.0 - highest) / min(4.0 * lowest - 4.0, -0.0001);
    const float3 lobes = max(-toLowest, toHighest);
    const float lobe = max(-0.1875, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0)) * sharpness;
    return Expand(clamp((lobe * (north + west + east + south) + center) / (4.0 * lobe + 1.0), 0.0, 0.999));
}

uint LensHash(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

// The grain's largest step on the gamma-encoded image at full strength (1): a mid-tone moves by about this
// much, ±10 levels of 255 at the old default of 0.35.
static const float kGrainStrength = 0.11;

float LensRandom(uint2 cell, uint salt) {
    return float(LensHash(cell.x * 73856093u ^ cell.y * 19349663u ^ salt) & 0xFFFF) / 65535.0;
}

// Smudges and specks on the lens, from 0 to 1: blobs of grease in a coarse grid, specks of dust in a fine
// one, each at a place and of a size of its own. The same every frame.
float LensDirt(float2 uv, float aspect) {
    float dirt = 0.0;
    const float2 p = uv * float2(aspect, 1.0);
    for (uint layer = 0; layer < 2; ++layer) {
        const float cells = layer == 0 ? 3.0 : 14.0;
        const float2 q = p * cells;
        const int2 base = int2(floor(q));
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const uint2 cell = uint2(base + int2(x, y) + 1000);
                const float2 center = float2(cell) - 1000.0 + float2(LensRandom(cell, layer * 2), LensRandom(cell, layer * 2 + 1));
                const float size = layer == 0 ? lerp(0.25, 0.6, LensRandom(cell, 7)) : lerp(0.04, 0.12, LensRandom(cell, 9));
                const float d = length(q - center) / size;
                const float strength = layer == 0 ? lerp(0.15, 0.5, LensRandom(cell, 11)) : (LensRandom(cell, 13) < 0.35 ? 1.0 : 0.0);
                dirt += strength * saturate(1.0 - d * d) * saturate(1.0 - d * d);
            }
        }
    }
    return saturate(dirt);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const int2 pixel = int2(position.xy);
    uint width;
    uint height;
    g_textures[g_push.scene].GetDimensions(width, height);
    const int2 scenePixel = pixel - int2(g_push.sceneX, g_push.sceneY);
    float3 color = 0.0;
    if (all(scenePixel >= 0) && all(scenePixel < int2(width, height))) {
        if (g_push.sharpness > 0.0) {
            color = Sharpen(scenePixel, int2(width, height), g_push.sharpness);
        } else {
            color = max(g_textures[g_push.scene].Load(int3(scenePixel, 0)).rgb, 0.0);
        }
        const float2 uv = (float2(scenePixel) + 0.5) / float2(width, height);
        const float2 fromCenter = (uv - 0.5) * float2(float(width) / float(height), 1.0);
        if (g_push.aberration > 0.0) {
            // Red and blue land a little apart towards the edges, as through a plain lens.
            const float2 shift = (uv - 0.5) * g_push.aberration * 0.012;
            color.r = max(g_textures[g_push.scene].SampleLevel(g_samplers[kSamplerLinearClamp], uv + shift, 0.0).r, 0.0);
            color.b = max(g_textures[g_push.scene].SampleLevel(g_samplers[kSamplerLinearClamp], uv - shift, 0.0).b, 0.0);
        }
        if (g_push.bloom != kNone) {
            const float3 bloom = max(g_textures[g_push.bloom].SampleLevel(g_samplers[kSamplerLinearClamp], uv, 0.0).rgb, 0.0);
            color = lerp(color, bloom, g_push.bloomShare);
            if (g_push.lensDirt > 0.0) {
                // The dirt scatters the light that crosses it: bright light in the view lights it up.
                color += bloom * LensDirt(uv, float(width) / float(height)) * g_push.lensDirt * 2.0;
            }
        }
        if (g_push.vignette > 0.0) {
            // Light falls off towards the corners, about as the fourth power of the cosine of the angle.
            const float r2 = dot(fromCenter, fromCenter);
            color *= lerp(1.0, 1.0 / ((1.0 + r2 * 0.6) * (1.0 + r2 * 0.6)), g_push.vignette);
        }
        color *= g_push.scale;
        color = g_push.toneMapper == 0 ? AgX(color) : PbrNeutral(color);
        if (g_push.grain > 0.0) {
            // Film grain, new each frame, added where the eye judges it: on the gamma-encoded image, so that a
            // step of grain looks the same size in every tone (ADR-0063). Strongest in the mid-tones, as silver
            // grains show most there; it fades out in deep shadows, where added in linear light it outgrew
            // the dark pixel it sat on, and a little in the highlights.
            const float noise = LensRandom(uint2(scenePixel), g_push.frame * 2654435761u) +
                                LensRandom(uint2(scenePixel), g_push.frame * 2246822519u + 1u) - 1.0;
            float3 encoded = pow(color, 1.0 / 2.2);
            const float tone = saturate(dot(encoded, float3(0.2126, 0.7152, 0.0722)));
            const float weight = smoothstep(0.02, 0.35, tone) * (1.0 - tone * 0.5);
            encoded = max(encoded + noise * g_push.grain * kGrainStrength * weight, 0.0);
            color = pow(encoded, 2.2);
        }
        color = ExpandHighlights(color, g_push.headroom);
    }
    if (g_push.layer != kNone) {
        const float4 layer = g_textures[g_push.layer].Load(int3(pixel, 0));
        color = layer.rgb + color * (1.0 - layer.a);
    }
    if (g_push.colorVision != 0) {
        color = Daltonize(color, g_push.colorVision);
    }
    if (g_push.hdr10 != 0) {
        return float4(PqEncode(max(Rec709ToRec2020(color), 0.0) * g_push.paperWhite), 1.0);
    }
    return float4(color, 1.0);
}
