// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The sky, wherever the mesh pass drew nothing (ADR-0036): one triangle over the screen at the far plane,
// which the depth test keeps only where the depth is still the cleared one.

#include "MeshTypes.slangh"

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen, at depth 0, the reverse-Z far plane.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    // Pixels count down from the top; clip-space Y points up (CONVENTIONS.md, section 5).
    const float2 uv = position.xy * frame.screenTexel;
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    const float3 direction = normalize(through.xyz / through.w - frame.cameraPosition);
    return float4(SkyRadiance(frame, direction), 1.0);
}
