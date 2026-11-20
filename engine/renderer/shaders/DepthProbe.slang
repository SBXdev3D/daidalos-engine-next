// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// A small grid of the scene's depth in meters, read back for an editor's overlays (ADR-0106): what lies behind
// a wall is drawn faint. Each cell keeps the farthest of four samples across it, so a line along an edge stays
// in front of the surface it lies on. The sky is far.

struct DepthProbePushConstants {
    uint depth;       // The G-buffer's depth, reversed: 1 at the near plane, 0 at the far one.
    uint output;      // width * height floats, rows from the top.
    uint width;       // Cells.
    uint height;
    uint renderWidth; // Pixels of the frame, drawn into the top-left corner of the depth.
    uint renderHeight;
    float nearPlane;
    float farPlane;
};

[[vk::push_constant]] DepthProbePushConstants g_push;
[[vk::binding(0, 0)]] Texture2D g_textures[];
[[vk::binding(2, 0)]] RWStructuredBuffer<float> g_buffers[];

static const float kSky = 1.0e9;

float Meters(float depth) {
    if (depth <= 0.0) {
        return kSky;
    }
    return g_push.nearPlane * g_push.farPlane / (depth * (g_push.farPlane - g_push.nearPlane) + g_push.nearPlane);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(g_push.width, g_push.height))) {
        return;
    }
    const float2 cell = float2(g_push.renderWidth, g_push.renderHeight) / float2(g_push.width, g_push.height);
    float farthest = 0.0;
    for (uint sample = 0; sample < 4; ++sample) {
        const float2 offset = float2(float(sample & 1) * 0.5 + 0.25, float(sample >> 1) * 0.5 + 0.25);
        const int2 pixel = min(int2((float2(id.xy) + offset) * cell), int2(g_push.renderWidth, g_push.renderHeight) - 1);
        farthest = max(farthest, Meters(g_textures[g_push.depth].Load(int3(pixel, 0)).r));
    }
    g_buffers[g_push.output][id.y * g_push.width + id.x] = farthest;
}
