// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The light each froxel scatters towards the camera, per meter (ADR-0051). Each frame, a point drawn at
// random in each froxel takes the light of one of the frame's lights, chosen among kFogCandidates drawn by
// power near the camera (the fog's alias table, FogSourceWeight) or by power alone, with even odds, weighed by what they would scatter towards
// the camera (resampled importance sampling, Talbot et al. 2005), with one shadow ray; its medium, the
// level's fog and the boxes, scatters a share of it. What the froxel held last frame, found where its center
// was then, is blended in, so that the noise of one ray per froxel averages out over frames, and the
// froxel's whole volume with it. A froxel that was out of view last frame takes the nearest edge of the
// view's history, with less weight; one lit by a light that moved, as a flashlight with the camera, keeps
// less of its history, which would lag behind.

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
    uint lightMap;       // The light map's flags (MeshPass::LightMapIndex), for lights that moved.
    uint previousLightCount;
    uint padding;
};

[[vk::push_constant]] FogPushConstants g_fog;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> g_volumeImages[];

// Lights weighed before one is kept. Drawn by power near the camera; a light next to a froxel far from the
// camera, among hundreds, such as an emissive panel, is still rarely one of them: its light comes in rare,
// bright samples, which the history spreads into slow blotches (ADR-0051).
static const uint kFogCandidates = 8;
// The history's share where it was out of view, read at the view's nearest edge, and where the froxel's light
// moved: less, so that the froxel catches up within a few frames.
static const float kEdgeHistoryWeight = 0.8;
static const float kMovingHistoryWeight = 0.8;

// What InScattered returns: the light, and whether the light it came from moved since the last frame.
struct FogLight {
    float3 light;
    bool moving;
};

// The light of the frame's lights at a point in the fog that it scatters towards the camera, per unit of its
// scattering coefficient: from one light chosen among candidates, with one shadow ray, unbiased.
FogLight InScattered(FrameData frame, float3 position, float3 view, inout uint random) {
    FogLight result;
    result.light = float3(0.0, 0.0, 0.0);
    result.moving = false;
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
        const float pick = Random(random);
        const LightChoice choice = ChooseLight(frame, frame.fogAliasBuffer, frame.aliasBuffer, pick,
                                               float4(Random(random), Random(random), Random(random), Random(random)));
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
        return result;
    }
    result.moving = (g_lightMaps[g_fog.lightMap][g_fog.previousLightCount + reservoir.light] & kLightMoved) != 0;
    const float weight = reservoir.weightSum / (float(kFogCandidates) * reservoir.target);
    const LightContact contact = ContactLight(frame, reservoir.light, reservoir.u, receiver);
    if (Unoccluded(frame, receiver, contact, g_fog.instanceBuffer, g_fog.materialBuffer)) {
        result.light = contact.illuminance * FogPhase(frame.fogAnisotropy, dot(contact.toLight, view)) * weight;
    }
    return result;
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
    bool moving = false;
    if (medium.extinction > 0.0) {
        const float3 view = normalize(position - frame.cameraPosition);
        const FogLight light = InScattered(frame, position, view, random);
        current.rgb = medium.scattering * light.light;
        moving = light.moving;
    }

    // Where the froxel's center was last frame. Out of view, the nearest edge of last frame's view: fog
    // changes slowly across it, and a froxel that starts over shows one ray's noise.
    float4 blended = current;
    if (g_fog.history != kNoTexture) {
        const float3 center = FroxelPoint(frame, (float2(id.xy) + 0.5) / float2(size.xy), (float(id.z) + 0.5) / float(size.z));
        const float4 clip = mul(frame.previousViewProjection, float4(center, 1.0));
        if (clip.w > 0.0) {
            const float2 uv = ClipToUv(clip);
            const float2 inView = saturate(uv);
            const float slice = FroxelSlice(frame, clip.w);
            const float4 past =
                g_volumeTextures[g_fog.history].SampleLevel(g_samplers[kSamplerLinearClamp], float3(inView, slice), 0.0);
            float weight = g_fog.historyWeight;
            if (any(inView != uv) || clip.w < frame.nearPlane) {
                weight = min(weight, kEdgeHistoryWeight);
            }
            if (moving) {
                weight = min(weight, kMovingHistoryWeight);
            }
            blended = lerp(current, past, weight);
        }
    }
    g_volumeImages[g_fog.scattering][id] = blended;
}
