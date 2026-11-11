// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Puts the mesh pass's light back together (ADR-0039): its ambient light and emission, plus the lights'
// diffuse light times the surface's diffuse color and their specular light times its specular albedo, as
// the denoiser left them, then the distance fog. Where nothing was drawn, what the mesh pass left there
// stays: the sky, or the fog's color.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct CompositePushConstants {
    uint frameBuffer;
    uint base;     // Ambient light, emission and the sky.
    uint diffuse;  // Diffuse light per unit of diffuse color.
    uint specular; // Specular light per unit of specular albedo.
    uint shading;  // The mesh pass's specular target, whose alpha is the specular albedo.
    uint albedo;   // The diffuse color.
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
    const float3 base = Read(g_composite.base, pixel).rgb;
    const float depth = Read(g_composite.depth, pixel).r;
    if (depth <= 0.0) {
        return float4(base, 1.0);
    }
    const float3 color = base + Read(g_composite.diffuse, pixel).rgb * Read(g_composite.albedo, pixel).rgb +
                         Read(g_composite.specular, pixel).rgb * Read(g_composite.shading, pixel).a;
    const float3 surface = WorldPosition(frame, position.xy * frame.screenTexel, depth);
    const float visibility = exp(-frame.fogDensity * distance(surface, frame.cameraPosition));
    return float4(lerp(frame.fogColor, color, visibility), 1.0);
}
