// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Draws Dear ImGui geometry. Matches PushConstants in engine/debugui/src/DebugUi.cpp.

struct PushConstants {
    float2 scale;
    float2 translate;
    uint textureIndex;
};

[[vk::push_constant]] PushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(1, 0)]] SamplerState g_samplers[3];

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

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.clipPosition = float4(input.position * g_push.scale + g_push.translate, 0.0, 1.0);
    output.uv = input.uv;
    // ImGui colors are sRGB, and the swapchain expects linear values.
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
    return input.color * g_textures[g_push.textureIndex].Sample(g_samplers[0], input.uv);
}
