// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// A channel of the G-buffer, in color.
// Motion vectors (ADR-0037): grey where nothing moved, more red where the surface came from the right, more
// green where it came from below, by up to 16 pixels.
// Normals: the world normal of each pixel, normal maps included, as stored: times 0.5 plus 0.5.

static const uint kShowMotion = 0;
static const uint kShowNormals = 1;

struct GBufferDebugPushConstants {
    uint texture;
    uint mode;
    float2 extent; // Pixels.
};

[[vk::push_constant]] GBufferDebugPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const float4 value = g_textures[g_push.texture].Load(int3(int2(position.xy), 0));
    if (g_push.mode == kShowNormals) {
        return float4(value.rgb, 1.0);
    }
    return float4(saturate(0.5 + value.xy * g_push.extent / 32.0), 0.5, 1.0);
}
