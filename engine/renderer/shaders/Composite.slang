// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Puts the frame's light back together (ADR-0039, ADR-0044): the emission of the G-buffer, plus the diffuse
// light times the surface's diffuse color and the specular light times its specular albedo, as the denoiser
// left them, then the distance fog. Where nothing was drawn, the sky shows, or the fog's color without one.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct CompositePushConstants {
    uint frameBuffer;
    uint baseColor; // The G-buffer's base color, and the metalness in alpha.
    uint emission;  // The G-buffer's emission.
    uint diffuse;   // Diffuse light per unit of diffuse color.
    uint specular;  // Specular light per unit of specular albedo.
    uint shading;   // The lighting pass's specular target, whose alpha is the specular albedo.
    uint depth;
    uint padding;
};

[[vk::push_constant]] CompositePushConstants g_composite;

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 Read(uint texture, int2 pixel) {
    return g_textures[texture].Load(int3(pixel, 0));
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_composite.frameBuffer][0];
    const int2 pixel = int2(position.xy);
    const float depth = Read(g_composite.depth, pixel).r;
    const float2 uv = position.xy * frame.screenTexel;
    if (depth <= 0.0) {
        if (frame.skyTexture == kNoTexture) {
            return float4(frame.fogColor, 1.0);
        }
        // Pixels count down from the top; clip-space Y points up (CONVENTIONS.md, section 5).
        const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
        return float4(SkyRadiance(frame, normalize(through.xyz / through.w - frame.cameraPosition)), 1.0);
    }
    const float4 baseMetallic = Read(g_composite.baseColor, pixel);
    const float3 diffuseColor = baseMetallic.rgb * (1.0 - baseMetallic.a);
    const float3 color = Read(g_composite.emission, pixel).rgb + Read(g_composite.diffuse, pixel).rgb * diffuseColor +
                         Read(g_composite.specular, pixel).rgb * Read(g_composite.shading, pixel).a;
    const float3 surface = WorldPosition(frame, uv, depth);
    const float visibility = exp(-frame.fogDensity * distance(surface, frame.cameraPosition));
    return float4(lerp(frame.fogColor, color, visibility), 1.0);
}
