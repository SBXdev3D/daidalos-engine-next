// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Bends the frame where distortion particles drew (ADR-0053): each pixel shows the copy of the frame a little
// aside, as far as the particles' offsets say, a fraction of the frame's height. Pixels without an offset
// show the copy where they are.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"

struct DistortionPushConstants {
    uint frameBuffer;
    uint background; // The frame before it was bent.
    uint distortion; // The particles' offsets.
    uint padding;
};

[[vk::push_constant]] DistortionPushConstants g_distortion;

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    // (-1, -1), (3, -1), (-1, 3): one triangle that covers the screen.
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    const FrameData frame = g_frames[g_distortion.frameBuffer][0];
    const int2 pixel = int2(position.xy);
    const float2 offset = g_textures[g_distortion.distortion].Load(int3(pixel, 0)).rg;
    if (all(offset == 0.0)) {
        return g_textures[g_distortion.background].Load(int3(pixel, 0));
    }
    // An offset of one is the frame's height, whatever its shape.
    const float2 aside = (position.xy + offset * float(frame.renderSize.y)) * frame.screenTexel;
    return g_textures[g_distortion.background].SampleLevel(g_samplers[kSamplerLinearClamp], FrameToTarget(frame, aside), 0.0);
}
