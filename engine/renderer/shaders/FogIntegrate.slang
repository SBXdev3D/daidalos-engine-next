// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Adds up the fog from the camera outwards (ADR-0051): each froxel of the result holds the light the fog
// in front of it scatters towards the camera, and the fraction of the light from behind that crosses it.
// Within a slice, the scattering and extinction are taken as even, and the light integrated exactly over
// its depth (Hillaire 2015).

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "Fog.slangh"

// Matches FogPushConstants in renderer/src/VolumetricFog.cpp.
struct FogPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint scattering; // Sampled: the lighting pass's scattered light and extinction.
    uint history;
    uint integrated; // Storage image: the fog in front of each froxel.
    uint width;
    uint height;
    uint slices;
    uint seed;
    float historyWeight;
    uint lightMap;
    uint previousLightCount;
    uint smoke;     // Storage image of the smoke's density (FogSmoke.slang), or kNoTexture.
    uint smokeList;
    uint padding;
};

[[vk::push_constant]] FogPushConstants g_fog;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> g_volumeImages[];
[[vk::binding(4, 0)]] [[vk::image_format("r32ui")]] RWTexture3D<uint> g_smokeImages[];

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_fog.width || id.y >= g_fog.height) {
        return;
    }
    const FrameData frame = g_frames[g_fog.frameBuffer][0];
    const float3 ray = ViewRay(frame, (float2(id.xy) + 0.5) / float2(g_fog.width, g_fog.height));
    // Meters along the ray per meter of depth.
    const float stretch = 1.0 / max(dot(ray, frame.cameraForward), 0.01);
    float3 scattered = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    float start = 0.0;
    for (uint slice = 0; slice < g_fog.slices; ++slice) {
        const float end = FroxelDepth(frame, float(slice + 1) / float(g_fog.slices));
        const float length = (end - start) * stretch;
        start = end;
        const float4 froxel = g_volumeTextures[g_fog.scattering].Load(int4(id.xy, slice, 0));
        const float extinction = max(froxel.a, 0.0);
        const float crossing = exp(-extinction * length);
        // The light scattered over the slice, each part of it dimmed by the slice in front of it.
        const float3 inSlice = extinction > 1e-6 ? froxel.rgb * ((1.0 - crossing) / extinction) : froxel.rgb * length;
        scattered += transmittance * inSlice;
        transmittance *= crossing;
        g_volumeImages[g_fog.integrated][uint3(id.xy, slice)] = float4(scattered, transmittance);
        if (g_fog.smoke != kNoTexture) {
            // The smoke is added anew each frame.
            g_smokeImages[g_fog.smoke][uint3(id.xy, slice)] = 0;
        }
    }
}
