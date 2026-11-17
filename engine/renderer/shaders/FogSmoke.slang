// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Smoke and steam in the volumetric fog (ADR-0062): each particle of an effect with a fog density adds it to
// the froxels around it, thinning to nothing at its edge, so that the fog's lighting lights it from within: the
// lamps and the flashlight's beam show in the smoke. A thread group per particle of the simulation's list;
// the fog's integration empties the froxels for the next frame.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "Fog.slangh"
#include "ParticleTypes.slangh"

// Matches FogPushConstants in renderer/src/VolumetricFog.cpp.
struct FogPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint scattering;
    uint history;
    uint integrated;
    uint width;
    uint height;
    uint slices;
    uint seed;
    float historyWeight;
    uint lightMap;
    uint previousLightCount;
    uint smoke;     // Storage image: the smoke's density in each froxel, in kSmokeScale units.
    uint smokeList; // The simulation's smoke particles: how many, then their indices.
    uint padding;
};

[[vk::push_constant]] FogPushConstants g_fog;
[[vk::binding(4, 0)]] [[vk::image_format("r32ui")]] RWTexture3D<uint> g_smokeImages[];
[[vk::binding(2, 0)]] StructuredBuffer<uint> g_smokeList[];
[[vk::binding(2, 0)]] StructuredBuffer<Particle> g_smokeParticles[];
[[vk::binding(2, 0)]] StructuredBuffer<ParticleEffectData> g_smokeEffects[];
[[vk::binding(2, 0)]] StructuredBuffer<float4> g_smokeCurves[];

// The most froxels one particle adds to: one that fills the view near the camera is cut short.
static const uint kMaxFroxels = 65536;

groupshared uint3 s_low;
groupshared uint3 s_count;
groupshared float4 s_sphere; // Center and radius.
groupshared float s_density;

float2 UvOf(FrameData frame, float3 position) {
    return ClipToUv(mul(frame.viewProjection, float4(position, 1.0)));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    if (group.x >= min(g_smokeList[g_fog.smokeList][0], kMaxSmokeParticles)) {
        return;
    }
    const FrameData frame = g_frames[g_fog.frameBuffer][0];
    if (thread == 0) {
        const Particle particle = g_smokeParticles[frame.particleData][g_smokeList[g_fog.smokeList][1 + group.x]];
        const ParticleEffectData effect = g_smokeEffects[frame.particleEffects][particle.effect];
        const float life = saturate(particle.age / particle.lifetime);
        const float4 scales = SampleCurveFrom(g_smokeCurves[frame.particleCurves], particle.effect, kCurveScales, life);
        const float4 color = SampleCurveFrom(g_smokeCurves[frame.particleCurves], particle.effect, kCurveColor, life);
        const float radius = max(particle.size * scales.x * 0.5, 0.01);
        s_sphere = float4(particle.position, radius);
        s_density = effect.fogDensity * color.a * (effect.fadeIn > 0.0 ? saturate(life / effect.fadeIn) : 1.0);

        // The froxels around it: across the view, from where its edges project; in depth, its near and far.
        const float3 right = normalize(cross(frame.cameraForward, float3(0.0, 1.0, 0.0)) + float3(1e-5, 0.0, 0.0));
        const float3 up = cross(right, frame.cameraForward);
        const float depth = dot(particle.position - frame.cameraPosition, frame.cameraForward);
        float2 low = float2(0.0, 0.0);
        float2 high = float2(1.0, 1.0);
        if (depth - radius > frame.nearPlane) {
            const float2 center = UvOf(frame, particle.position);
            const float2 reach = abs(UvOf(frame, particle.position + right * radius) - center) +
                                 abs(UvOf(frame, particle.position + up * radius) - center);
            const float extent = max(reach.x, reach.y) * 1.2;
            low = center - extent;
            high = center + extent;
        }
        const float3 size = float3(g_fog.width, g_fog.height, g_fog.slices);
        const float nearSlice = FroxelSlice(frame, depth - radius) * size.z;
        const float farSlice = FroxelSlice(frame, depth + radius) * size.z;
        const float3 first = clamp(floor(float3(low, nearSlice) * float3(size.xy, 1.0)), 0.0, size - 1.0);
        const float3 last = clamp(floor(float3(high, farSlice) * float3(size.xy, 1.0)), 0.0, size - 1.0);
        s_low = uint3(first);
        s_count = depth + radius > frame.nearPlane && s_density > 0.0 ? uint3(last - first) + 1 : uint3(0, 0, 0);
    }
    GroupMemoryBarrierWithGroupSync();
    const uint total = min(s_count.x * s_count.y * s_count.z, kMaxFroxels);
    const float3 size = float3(g_fog.width, g_fog.height, g_fog.slices);
    for (uint at = thread; at < total; at += 64) {
        const uint3 froxel = s_low + uint3(at % s_count.x, (at / s_count.x) % s_count.y, at / (s_count.x * s_count.y));
        const float3 point = FroxelPoint(frame, (float2(froxel.xy) + 0.5) / size.xy, (float(froxel.z) + 0.5) / size.z);
        const float d = distance(point, s_sphere.xyz) / s_sphere.w;
        if (d < 1.0) {
            const float falloff = (1.0 - d * d) * (1.0 - d * d);
            InterlockedAdd(g_smokeImages[g_fog.smoke][froxel], uint(s_density * falloff * kSmokeScale));
        }
    }
}
