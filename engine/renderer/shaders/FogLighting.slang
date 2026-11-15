// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The light each froxel scatters towards the camera, per meter (ADR-0051). Each frame, a point drawn at
// random in each froxel takes the light of one of the frame's lights, chosen among kFogCandidates drawn by
// power, weighed by what they would scatter towards the camera (resampled importance sampling, Talbot et
// al. 2005), with one shadow ray; its medium, the level's fog and the boxes, scatters a share of it. What the
// froxel held last frame, found where its center was then, is blended in, so that the noise of one ray per
// froxel averages out over frames, and the froxel's whole volume with it.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

// Matches FogPushConstants in renderer/src/VolumetricFog.cpp.
struct FogPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint scattering; // Storage image: this frame's scattered light and extinction.
    uint history;    // Sampled: last frame's, or kNoTexture.
    uint integrated; // Storage image of the integration pass.
    uint width;
    uint height;
    uint slices;
    uint seed;
    float historyWeight; // The share of the history in the blend.
    uint padding;
};

[[vk::push_constant]] FogPushConstants g_fog;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> g_volumeImages[];

// Lights weighed before one is kept. Drawn by power over the whole level, a light next to the froxel among
// hundreds, such as an emissive panel, is rarely one of them: its light comes in rare, bright samples, which
// the history spreads into slow blotches (ADR-0051).
static const uint kFogCandidates = 8;

// The light of the frame's lights at a point in the fog that it scatters towards the camera, per unit of its
// scattering coefficient: from one light chosen among candidates, with one shadow ray, unbiased.
float3 InScattered(FrameData frame, float3 position, float3 view, inout uint random) {
    Receiver receiver;
    receiver.position = position;
    receiver.origin = position;
    receiver.faceNormal = view;
    receiver.normal = view;
    receiver.view = -view;
    receiver.diffuseColor = float3(1.0, 1.0, 1.0);
    receiver.f0 = float3(0.0, 0.0, 0.0);
    receiver.roughness = 1.0;
    receiver.response = kRespondEverywhere;
    receiver.emissiveSpecular = true;
    receiver.nearest = kNearestLight;

    Reservoir reservoir = EmptyReservoir();
    for (uint candidate = 0; candidate < kFogCandidates; ++candidate) {
        const LightChoice choice = ChooseLight(frame, float4(Random(random), Random(random), Random(random), Random(random)));
        const float2 u = float2(Random(random), Random(random));
        if (choice.index == kNoLight) {
            break;
        }
        const LightContact contact = ContactLight(frame, choice.index, u, receiver);
        const float target =
            contact.valid ? Luminance(contact.illuminance) * FogPhase(frame.fogAnisotropy, dot(contact.toLight, view)) : 0.0;
        Stream(reservoir, choice.index, u, target / choice.probability, target, Random(random));
    }
    if (reservoir.light == kNoLight || reservoir.target <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float weight = reservoir.weightSum / (float(kFogCandidates) * reservoir.target);
    const LightContact contact = ContactLight(frame, reservoir.light, reservoir.u, receiver);
    if (!Unoccluded(frame, receiver, contact, g_fog.instanceBuffer, g_fog.materialBuffer)) {
        return float3(0.0, 0.0, 0.0);
    }
    return contact.illuminance * FogPhase(frame.fogAnisotropy, dot(contact.toLight, view)) * weight;
}

// The point of the view at texture coordinates uv, slice in [0, 1].
float3 FroxelPoint(FrameData frame, float2 uv, float slice) {
    const float3 ray = ViewRay(frame, uv);
    return frame.cameraPosition + ray * (FroxelDepth(frame, slice) / max(dot(ray, frame.cameraForward), 0.01));
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint3 size = uint3(g_fog.width, g_fog.height, g_fog.slices);
    if (any(id >= size)) {
        return;
    }
    const FrameData frame = g_frames[g_fog.frameBuffer][0];
    uint random = Pcg((id.z * size.y + id.y) * size.x + id.x) ^ Pcg(g_fog.seed * 9781u + 1u);
    NextRandom(random);

    // A point anywhere in the froxel, and the medium there.
    const float3 inFroxel = float3(Random(random), Random(random), Random(random));
    const float3 position = FroxelPoint(frame, (float2(id.xy) + inFroxel.xy) / float2(size.xy),
                                        (float(id.z) + inFroxel.z) / float(size.z));
    const FogMedium medium = FogMediumAt(frame, position);
    float4 current = float4(0.0, 0.0, 0.0, medium.extinction);
    if (medium.extinction > 0.0) {
        const float3 view = normalize(position - frame.cameraPosition);
        current.rgb = medium.scattering * InScattered(frame, position, view, random);
    }

    // Where the froxel's center was last frame.
    float4 blended = current;
    if (g_fog.history != kNoTexture) {
        const float3 center = FroxelPoint(frame, (float2(id.xy) + 0.5) / float2(size.xy), (float(id.z) + 0.5) / float(size.z));
        const float4 clip = mul(frame.previousViewProjection, float4(center, 1.0));
        if (clip.w > 0.0) {
            const float2 uv = ClipToUv(clip);
            if (all(uv >= 0.0) && all(uv <= 1.0)) {
                const float slice = FroxelSlice(frame, clip.w);
                const float4 past =
                    g_volumeTextures[g_fog.history].SampleLevel(g_samplers[kSamplerLinearClamp], float3(uv, slice), 0.0);
                blended = lerp(current, past, g_fog.historyWeight);
            }
        }
    }
    g_volumeImages[g_fog.scattering][id] = blended;
}
