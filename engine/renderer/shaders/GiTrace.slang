// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The indirect diffuse light of each pixel (ADR-0040): one ray from the pre-pass's surface in a random
// direction, more often near its normal, and the light that comes back along it: the light the radiance
// cache holds where the ray hits, or the sky where it leaves. Rays in that distribution, averaged, give
// the light per unit of diffuse color; one per pixel is noisy, which the denoiser filters.
// With g_gi.mode 1, a debug view: each pixel shows the light its own surface's cell holds.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_storageImages[];

float3 PositionAt(FrameData frame, int2 pixel) {
    const int2 clamped = clamp(pixel, int2(0, 0), int2(g_gi.width, g_gi.height) - 1);
    const float depth = g_textures[g_gi.depth].Load(int3(clamped, 0)).r;
    return WorldPosition(frame, (float2(clamped) + 0.5) * frame.screenTexel, depth);
}

// The surface's own orientation, from the positions of the neighbours on the side closest in depth, so as
// not to take it across an edge; towards the camera.
float3 FaceNormalAt(FrameData frame, int2 pixel, float3 position) {
    const float3 left = PositionAt(frame, pixel - int2(1, 0));
    const float3 right = PositionAt(frame, pixel + int2(1, 0));
    const float3 up = PositionAt(frame, pixel - int2(0, 1));
    const float3 down = PositionAt(frame, pixel + int2(0, 1));
    const float3 across = distance(left, position) < distance(right, position) ? position - left : right - position;
    const float3 along = distance(up, position) < distance(down, position) ? position - up : down - position;
    float3 normal = normalize(cross(along, across));
    if (dot(normal, frame.cameraPosition - position) < 0.0) {
        normal = -normal;
    }
    return normal;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_gi.frameBuffer][0];
    if (any(id.xy >= uint2(g_gi.width, g_gi.height))) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float depth = g_textures[g_gi.depth].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        g_storageImages[g_gi.output][id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) * frame.screenTexel, depth);
    const float3 faceNormal = FaceNormalAt(frame, pixel, position);
    if (g_gi.mode == 1) {
        g_storageImages[g_gi.output][id.xy] = float4(CachedRadiance(frame, position, faceNormal), 1.0);
        return;
    }
    float3 normal = normalize(g_textures[g_gi.normals].Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    uint random = Pcg((id.y * g_gi.width + id.x) * 9781u + g_gi.seed * 26699u);

    RayDesc ray;
    ray.Direction = CosineDirection(normal, float2(Random(random), Random(random)));
    if (dot(ray.Direction, faceNormal) <= 0.0) {
        ray.Direction = reflect(ray.Direction, faceNormal);
    }
    // A position from depth is less precise than a hit's: farther off the surface, the farther it is.
    const float viewDistance = distance(position, frame.cameraPosition);
    ray.Origin = OffsetOrigin(position, faceNormal) + faceNormal * (0.002 * viewDistance);
    ray.TMin = 0.0;
    ray.TMax = 1e6;
    const Hit hit = TraceClosest(ray, frame.accelerationStructure, g_gi.instanceBuffer, g_gi.materialBuffer);
    float3 light;
    if (hit.found) {
        const InstanceData instance = g_instances[g_gi.instanceBuffer][hit.instance];
        float3 hitNormal = normalize(FaceNormal(instance, hit.primitive));
        if (dot(hitNormal, ray.Direction) > 0.0) {
            hitNormal = -hitNormal;
        }
        light = CachedRadiance(frame, ray.Origin + ray.Direction * hit.distance, hitNormal);
    } else {
        light = SkyRadiance(frame, ray.Direction);
    }
    g_storageImages[g_gi.output][id.xy] = float4(light, 1.0);
}
