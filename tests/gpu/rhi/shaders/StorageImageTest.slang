// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Adds (x, y, 1, 0.5) to each texel of a storage image: run twice, it holds twice that.

struct Push {
    uint image;
};

[[vk::push_constant]] Push g_push;
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const float4 previous = g_storageImages[g_push.image][id.xy];
    g_storageImages[g_push.image][id.xy] = previous + float4(float(id.x), float(id.y), 1.0, 0.5);
}
