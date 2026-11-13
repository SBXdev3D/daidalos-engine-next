// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The motion vectors of the G-buffer, in color (ADR-0037): grey where nothing moved, more red where the
// surface came from the right, more green where it came from below, by up to 16 pixels.

struct MotionDebugPushConstants {
    uint motionTexture;
    float2 extent; // Pixels.
};

[[vk::push_constant]] MotionDebugPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const float2 motion = g_textures[g_push.motionTexture].Load(int3(int2(position.xy), 0)).xy * g_push.extent;
    return float4(saturate(0.5 + motion / 32.0), 0.5, 1.0);
}
