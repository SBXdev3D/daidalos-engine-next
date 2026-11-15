// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Puts the frame's light back together (ADR-0039, ADR-0044): the emission of the G-buffer, plus the diffuse
// light times the surface's diffuse color and the specular light times its specular albedo, as the denoiser
// left them, seen through the volumetric fog in front of them (ADR-0051). Where nothing was drawn, the sky
// shows, through the fog up to the far plane.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "Fog.slangh"

struct CompositePushConstants {
    uint frameBuffer;
    uint baseColor; // The G-buffer's base color, and the metalness in alpha.
    uint emission;  // The G-buffer's emission.
    uint diffuse;   // Diffuse light per unit of diffuse color.
    uint specular;  // Specular light per unit of specular albedo.
    uint shading;   // The lighting pass's specular target, whose alpha is the specular albedo.
    uint depth;
    uint flags;
};

// Matches the flags in renderer/src/MeshPass.cpp: the fog alone, over black.
static const uint kFogOnly = 1;

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
    const bool fogOnly = (g_composite.flags & kFogOnly) != 0;
    if (depth <= 0.0) {
        // Pixels count down from the top; clip-space Y points up (CONVENTIONS.md, section 5).
        const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
        const float3 sky = fogOnly ? float3(0.0, 0.0, 0.0)
                                   : SkyRadiance(frame, normalize(through.xyz / through.w - frame.cameraPosition));
        return float4(ThroughFog(FogAt(frame, uv, frame.farPlane, uint2(pixel)), sky), 1.0);
    }
    float3 color = float3(0.0, 0.0, 0.0);
    if (!fogOnly) {
        const float4 baseMetallic = Read(g_composite.baseColor, pixel);
        const float3 diffuseColor = baseMetallic.rgb * (1.0 - baseMetallic.a);
        color = Read(g_composite.emission, pixel).rgb + Read(g_composite.diffuse, pixel).rgb * diffuseColor +
                Read(g_composite.specular, pixel).rgb * Read(g_composite.shading, pixel).a;
    }
    return float4(ThroughFog(FogAt(frame, uv, LinearDepth(frame, depth), uint2(pixel)), color), 1.0);
}
