// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Glass (ADR-0042), drawn over the finished frame, which it reads as its background: the G-buffer
// leaves glass out. A pixel of glass is what its surface reflects, as much as Fresnel says, plus
// what goes through it, tinted by its base color:
// - thin glass, such as a window, bends nothing: what goes through is the frame behind the pixel;
// - thick glass is a closed mesh: a ray bends where it enters, crosses the glass to its far side, losing
//   some of each color on the way (Beer-Lambert), bends again as it leaves, and finds what is there.
// What a ray finds is read from the frame when it is on the screen and in view there; otherwise it is
// shaded with its emission and the frame's lights, shadows included, but without indirect light: only where
// it counts, since glass reflects little but at grazing angles. The frame's lights shine on the glass
// itself, as on any surface, which makes highlights. Lights are sampled (Lights.slangh): one among a few,
// with one shadow ray, for what a ray finds and for the highlights.
// The glass tests the G-buffer's depth itself, since it reads it: it has no depth target.

#include "MeshCommon.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"
import Bsdf;

// How far, relative to its distance, the surface the frame shows at a point may be from it for the frame's
// color to be the point's.
static const float kScreenDepthTolerance = 0.03;
// A reflection weaker than this is not worth lighting what it shows, out of view: only its emission counts.
static const float kLitReflection = 0.1;
// Sides a ray inside thick glass meets at most before it is given up, as black.
static const uint kInsideBounces = 4;

// Where a point is in the frame, and whether the frame shows it there.
bool OnScreen(FrameData frame, float3 position, out int2 pixel) {
    const float4 clip = mul(frame.viewProjection, float4(position, 1.0));
    pixel = int2(0, 0);
    if (clip.w <= frame.nearPlane) {
        return false;
    }
    const float2 uv = ClipToUv(clip);
    if (any(uv < 0.0) || any(uv >= 1.0)) {
        return false;
    }
    pixel = int2(uv / frame.screenTexel);
    const float shown = g_textures[frame.depthTexture].Load(int3(pixel, 0)).r;
    return shown > 0.0 && abs(LinearDepth(frame, shown) - clip.w) < kScreenDepthTolerance * clip.w;
}

// The light that comes back along a ray: what the frame shows where it hits, or the hit shaded (lit or not),
// or the sky. Emissive lights show: glass is a mirror, whose highlights leave them out (ADR-0044).
float3 TraceLight(FrameData frame, RayDesc ray, bool lit, inout uint random) {
    const Hit hit = TraceClosest(ray, frame.accelerationStructure, g_push.instanceBuffer, g_push.materialBuffer);
    if (!hit.found) {
        return SkyRadiance(frame, ray.Direction);
    }
    const float3 position = ray.Origin + ray.Direction * hit.distance;
    int2 pixel;
    if (OnScreen(frame, position, pixel)) {
        return g_textures[frame.backgroundTexture].Load(int3(pixel, 0)).rgb;
    }
    const Surface surface = ShadeHit(hit, ray.Direction, frame, g_push.instanceBuffer, g_push.materialBuffer);
    if (!lit) {
        return surface.emission;
    }
    return surface.emission + SampleDirectLight(frame, ReceiverOf(surface, -ray.Direction, kRespondBsdf), random,
                                                g_push.instanceBuffer, g_push.materialBuffer);
}

// The highlights of the frame's lights on a smooth surface, with their shadows: as the smoothest surface the
// G-buffer holds, so that a light the size of a point still shows. Emissive lights are left out: the
// reflection ray shows them.
float3 Highlights(FrameData frame, float3 position, float3 normal, float3 faceNormal, float3 view, inout uint random) {
    Receiver receiver;
    receiver.position = position;
    receiver.origin = OffsetOrigin(position, faceNormal);
    receiver.faceNormal = faceNormal;
    receiver.normal = normal;
    receiver.view = view;
    receiver.diffuseColor = float3(0.0, 0.0, 0.0);
    receiver.f0 = float3(0.04, 0.04, 0.04);
    receiver.roughness = 0.045;
    receiver.response = kRespondBsdf;
    receiver.emissiveSpecular = false;
    receiver.nearest = kNearestLight;
    return SampleDirectLight(frame, receiver, random, g_push.instanceBuffer, g_push.materialBuffer);
}

// What reaches the eye through thick glass, entering at position along direction, already bent; or a
// negative value if the glass has no far side, which makes it thin.
float3 ThroughVolume(FrameData frame, MaterialData material, float3 position, float3 faceNormal, float3 direction,
                     inout uint random) {
    RayDesc ray;
    ray.Origin = position - faceNormal * 1e-3;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 1e6;
    float3 absorbed = float3(1.0, 1.0, 1.0);
    // Light that a side reflects back in, past the critical angle, goes round a few more times.
    for (uint side = 0; side < kInsideBounces; ++side) {
        const Hit exit = TraceClosest(ray, frame.accelerationStructure, g_push.instanceBuffer, g_push.materialBuffer, kMaskGlass);
        if (!exit.found) {
            return float3(-1.0, -1.0, -1.0);
        }
        absorbed *= exp(-material.absorption * exit.distance);
        float3 outward = normalize(FaceNormal(g_instances[g_push.instanceBuffer][exit.instance], exit.primitive));
        if (dot(outward, ray.Direction) < 0.0) {
            outward = -outward;
        }
        const float3 exitPosition = ray.Origin + ray.Direction * exit.distance;
        const float cosine = dot(ray.Direction, outward);
        const float reflected = FresnelDielectric(cosine, material.ior);
        if (reflected < 1.0) {
            RayDesc leaving;
            // From just inside: what touches the glass there, such as the floor under a block, is in front.
            leaving.Origin = exitPosition - outward * 1e-3;
            leaving.Direction = refract(ray.Direction, -outward, material.ior);
            leaving.TMin = 0.0;
            leaving.TMax = 1e6;
            return absorbed * (1.0 - reflected) * TraceLight(frame, leaving, true, random);
        }
        // All of it is reflected back inside.
        ray.Origin = exitPosition - outward * 1e-3;
        ray.Direction = reflect(ray.Direction, outward);
    }
    return float3(0.0, 0.0, 0.0);
}

float4 PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    // Behind what the G-buffer drew: hidden. Reverse-Z: nearer is larger.
    const int2 pixel = int2(input.clipPosition.xy);
    if (input.clipPosition.z < g_textures[frame.depthTexture].Load(int3(pixel, 0)).r) {
        discard;
    }
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];
    const float4 baseColor = BaseColor(material, input.uv);
    const float3 normal = SurfaceNormal(input, isFrontFace, material, frame.normalStrength);
    const float3 toCamera = frame.cameraPosition - input.worldPosition;
    const float cameraDistance = length(toCamera);
    const float3 view = toCamera / max(cameraDistance, 0.0001);
    // The face's own normal, towards the camera, from which rays start.
    float3 faceNormal = normalize(cross(ddy(input.worldPosition), ddx(input.worldPosition)));
    if (dot(faceNormal, view) < 0.0) {
        faceNormal = -faceNormal;
    }
    const float cosine = saturate(dot(normal, view));
    const float reflected = FresnelDielectric(cosine, 1.0 / material.ior);
    uint random = Pcg(uint(pixel.y) * 65521u + uint(pixel.x)) ^ Pcg(frame.frameIndex * 26699u + 7u);

    RayDesc ray;
    ray.Origin = OffsetOrigin(input.worldPosition, faceNormal);
    ray.Direction = reflect(-view, normal);
    if (dot(ray.Direction, faceNormal) <= 0.0) {
        ray.Direction = reflect(ray.Direction, faceNormal);
    }
    ray.TMin = 0.0;
    ray.TMax = 1e6;
    const float3 reflection = TraceLight(frame, ray, reflected > kLitReflection, random) +
                              Highlights(frame, input.worldPosition, normal, faceNormal, view, random) /
                                  max(reflected, 0.001);

    const float3 behind = g_textures[frame.backgroundTexture].Load(int3(pixel, 0)).rgb;
    float3 through = behind;
    if (material.thin == 0) {
        const float3 bent = refract(-view, normal, 1.0 / material.ior);
        const float3 volume = ThroughVolume(frame, material, input.worldPosition, faceNormal, bent, random);
        through = volume.r >= 0.0 ? volume : behind;
    }
    // The fog between the camera and the glass veils its reflection; what is behind the glass is fogged
    // already, as the frame drew it.
    const float visibility = exp(-frame.fogDensity * cameraDistance);
    const float3 surface = reflected * lerp(frame.fogColor, reflection, visibility);
    return float4(surface + (1.0 - reflected) * material.transmission * baseColor.rgb * through, 1.0);
}
