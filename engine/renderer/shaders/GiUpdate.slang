// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Fills the radiance cache (ADR-0040): one path from the camera per g_gi.spacing by g_gi.spacing pixels,
// through a random point of its tile. At each surface it meets, a path gathers the light of one light
// (DirectDiffuseLight), then goes on in a random direction, as off a matte surface. Emission adds nothing:
// emissive lights are sampled as lights, and emission that is not a light is only seen (ADR-0044).
// After kVertices surfaces, it ends where its last ray hits, with the light the cache already holds
// there: light bounces as many times as the frames go on. Each surface's cell then receives the light
// the surface sends back along the path; what reaches it from the surfaces after it is clamped.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"

static const uint kVertices = 3;

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_gi.frameBuffer][0];
    const uint2 tiles = (uint2(g_gi.width, g_gi.height) + g_gi.spacing - 1) / g_gi.spacing;
    if (any(id.xy >= tiles)) {
        return;
    }
    uint random = Pcg((id.y * tiles.x + id.x) * 9781u + g_gi.seed * 6271u);

    // Through a random point of the tile; the jittered projection does not matter here.
    const float2 pixel = float2(id.xy * g_gi.spacing) + float2(Random(random), Random(random)) * float(g_gi.spacing);
    const float2 uv = min(pixel / float2(g_gi.width, g_gi.height), 1.0);
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    RayDesc ray;
    ray.Origin = frame.cameraPosition;
    ray.Direction = normalize(through.xyz / through.w - frame.cameraPosition);
    ray.TMin = frame.nearPlane;
    ray.TMax = frame.farPlane;

    uint cells[kVertices];
    float3 sent[kVertices];   // Each surface's own light: emission and direct light.
    float3 albedos[kVertices];
    uint vertices = 0;
    float3 tail = float3(0.0, 0.0, 0.0); // The light that reaches the last surface along the path.
    for (uint bounce = 0; bounce <= kVertices; ++bounce) {
        const Hit hit = TraceClosest(ray, frame.accelerationStructure, g_gi.instanceBuffer, g_gi.materialBuffer, kMaskIndirect);
        if (!hit.found) {
            tail = bounce == 0 ? float3(0.0, 0.0, 0.0) : SkyRadiance(frame, ray.Direction);
            break;
        }
        const Surface surface = ShadeHit(hit, ray.Direction, frame, g_gi.instanceBuffer, g_gi.materialBuffer);
        if (bounce == kVertices) {
            tail = CachedRadiance(frame, surface.position, surface.faceNormal);
            break;
        }
        cells[vertices] = FindCellOf(frame, surface.position, surface.faceNormal, true);
        sent[vertices] = DirectDiffuseLight(surface, frame, random);
        albedos[vertices] = surface.diffuseColor;
        ++vertices;
        if (all(surface.diffuseColor <= 0.0)) {
            break; // A metal: no diffuse light goes on.
        }
        ray.Direction = CosineDirection(surface.normal, float2(Random(random), Random(random)));
        if (dot(ray.Direction, surface.faceNormal) <= 0.0) {
            ray.Direction = reflect(ray.Direction, surface.faceNormal);
        }
        ray.Origin = OffsetOrigin(surface.position, surface.faceNormal);
        ray.TMin = 0.0;
        ray.TMax = 1e6;
    }

    // From the last surface back to the first, each sends its own light and reflects what reaches it, which
    // bounced off another surface, and is clamped (ADR-0045).
    float3 light = tail;
    for (int vertex = int(vertices) - 1; vertex >= 0; --vertex) {
        light = sent[vertex] + albedos[vertex] * ClampBounce(light);
        AddToCell(cells[vertex], light);
    }
}
