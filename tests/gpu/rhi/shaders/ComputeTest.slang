// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Writes value * 2 + 1 at each index of a storage buffer, and, from thread 0, the arguments of an indirect
// draw of one triangle.

struct Push {
    uint values;
    uint arguments;
    uint count;
};

[[vk::push_constant]] Push g_push;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> g_buffers[];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x < g_push.count) {
        g_buffers[g_push.values][id.x] = id.x * 2 + 1;
    }
    if (id.x == 0) {
        g_buffers[g_push.arguments][0] = 3; // Vertices.
        g_buffers[g_push.arguments][1] = 1; // Instances.
        g_buffers[g_push.arguments][2] = 0;
        g_buffers[g_push.arguments][3] = 0;
    }
}
