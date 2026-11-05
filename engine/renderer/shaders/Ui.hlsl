// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Draws the game's interface, a ui::Canvas, over the tonemapped image (ADR-0027). Matches PushConstants
// in engine/renderer/src/UiPass.cpp.

struct PushConstants {
    float2 scale;
    float2 translate;
    uint textureIndex;
    // Matches ui::Shading.
    uint shading;
};

[[vk::push_constant]] PushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[4];

// Matches rhi::SamplerIndex.
static const uint kSamplerLinearClamp = 0;
static const uint kShadingSolid = 0;
static const uint kShadingImage = 1;

struct VertexInput {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD;
    [[vk::location(2)]] float4 color : COLOR;
};

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD;
    [[vk::location(1)]] float4 color : COLOR;
};

float3 SrgbToLinear(float3 color) {
    return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.clipPosition = float4(input.position * g_push.scale + g_push.translate, 0.0, 1.0);
    output.uv = input.uv;
    // Canvas colors are sRGB, and the back buffer expects linear values.
    output.color = float4(SrgbToLinear(input.color.rgb), input.color.a);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
    if (g_push.shading == kShadingSolid) {
        return input.color;
    }
    const float4 texel = g_textures[g_push.textureIndex].Sample(g_samplers[kSamplerLinearClamp], input.uv);
    if (g_push.shading == kShadingImage) {
        return input.color * texel;
    }
    // A distance field, 0.5 on the outline: the edge fades over about one pixel of the screen.
    const float distance = texel.r;
    const float width = max(fwidth(distance) * 0.5, 1.0 / 255.0);
    const float coverage = smoothstep(0.5 - width, 0.5 + width, distance);
    return float4(input.color.rgb, input.color.a * coverage);
}
