// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Writes (x, y, 1, 0.5) to each texel of a half-float storage image, through the array that declares that
// format; the array of full floats shares its binding.

struct Push {
    uint image;
};

[[vk::push_constant]] Push g_push;
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_halfStorageImages[];

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    g_halfStorageImages[g_push.image][id.xy] = float4(float(id.x), float(id.y), 1.0, 0.5);
}
