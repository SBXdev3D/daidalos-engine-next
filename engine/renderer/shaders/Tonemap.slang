// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Brings the HDR image into the display's range (ADR-0022), drawn as one triangle over the screen. The
// target is an sRGB image, which encodes the linear result; or, before FXAA, an image that keeps the
// encoded colors and their luma in alpha (ADR-0026).

struct TonemapPushConstants {
    uint hdrTexture;
    float exposure;
    uint encode; // 1 to write sRGB-encoded colors and their luma.
};

[[vk::push_constant]] TonemapPushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// Khronos PBR Neutral: colors below 0.76 stay as they are, so textures keep their look, and brighter
// ones are compressed towards white without changing their hue.
float3 PbrNeutral(float3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
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

float3 LinearToSrgb(float3 color) {
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const float3 hdr = g_textures[g_push.hdrTexture].Load(int3(int2(position.xy), 0)).rgb;
    const float3 color = PbrNeutral(max(hdr, 0.0) * g_push.exposure);
    if (g_push.encode != 0) {
        const float3 encoded = LinearToSrgb(saturate(color));
        return float4(encoded, dot(encoded, float3(0.299, 0.587, 0.114)));
    }
    return float4(color, 1.0);
}
