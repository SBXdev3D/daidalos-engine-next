// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The reference path tracer (ADR-0034): one path per pixel per frame, averaged with the paths of earlier
// frames until the view changes. It shades hits with the materials and lights of the mesh pass and its
// BSDF (Bsdf.slang): it is the image that real-time techniques are compared with. It has no fog: light
// bounces as far as the paths go. A path that leaves the scene sees the background radiance.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
import Bsdf;

struct PathTracerPushConstants {
    uint frameBuffer;
    uint instanceBuffer;
    uint materialBuffer;
    uint output;      // Storage image: the average of the paths so far.
    uint sampleIndex; // Paths already averaged in output; 0 starts over.
    uint maxBounces;  // Surfaces a path meets at most; 1 is direct light only.
    uint seed;
    uint width;
    uint height;
    uint jitter; // 1 to spread the paths over the pixel, 0 to send them all through its center.
    uint padding1;
    uint padding2;
    float3 background;
    float padding3;
};

[[vk::push_constant]] PathTracerPushConstants g_path;
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];

static const float kPi = 3.14159265;
// PCG (O'Neill 2014): a small, good random number generator.
uint NextRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint state) {
    return float(NextRandom(state) >> 8) * (1.0 / 16777216.0);
}

// Light that reaches the surface straight from the frame's lights and leaves towards view: their
// illuminance, with the mesh pass's falloff (ADR-0043), times the BSDF.
// Like the mesh pass, it shades with the light's center and aims its shadow ray at a random point of the
// light's sphere (ADR-0039).
float3 DirectLight(Surface surface, float3 view, FrameData frame, inout uint random) {
    float3 total = float3(0.0, 0.0, 0.0);
    for (uint index = 0; index < frame.lightCount; ++index) {
        const LightData light = LightAt(g_lights[frame.lightBuffer][index], surface.position);
        const float3 offset = light.position - surface.position;
        const float distanceToLight = length(offset);
        if (distanceToLight >= light.range) {
            continue;
        }
        const float3 toLight = offset / max(distanceToLight, 0.0001);
        const float attenuation = Attenuation(light, distanceToLight, toLight);
        if (attenuation <= 0.0 || dot(surface.faceNormal, toLight) <= 0.0) {
            continue;
        }
        const float3 reflected = light.color * attenuation *
                                 EvaluateBsdf(surface.normal, view, toLight, surface.diffuseColor, surface.f0, surface.roughness);
        if (all(reflected <= 0.0)) {
            continue;
        }
        const float2 u = float2(Random(random), Random(random));
        const float3 toPoint = SampleLightPoint(light.position, light.radius, surface.position, u) - surface.position;
        const float distanceToPoint = length(toPoint);
        if (LightReaches(surface.position, surface.faceNormal, toPoint / max(distanceToPoint, 0.0001), distanceToPoint,
                         light.range, frame.accelerationStructure, g_path.instanceBuffer, g_path.materialBuffer)) {
            total += reflected;
        }
    }
    return total;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_path.frameBuffer][0];
    const uint2 size = uint2(g_path.width, g_path.height);
    if (any(id.xy >= size)) {
        return;
    }
    uint random = (id.y * size.x + id.x) * 9781u + g_path.seed * 6271u + g_path.sampleIndex * 26699u;
    NextRandom(random);

    // A camera ray through a random point of the pixel. Pixels count down from the top; clip-space Y
    // points up (CONVENTIONS.md, section 5).
    const float2 inPixel = g_path.jitter != 0 ? float2(Random(random), Random(random)) : float2(0.5, 0.5);
    const float2 uv = (float2(id.xy) + inPixel) / float2(size);
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    RayDesc ray;
    ray.Origin = frame.cameraPosition;
    ray.Direction = normalize(through.xyz / through.w - frame.cameraPosition);
    ray.TMin = frame.nearPlane;
    ray.TMax = 1e6;

    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    for (uint bounce = 0; bounce < g_path.maxBounces; ++bounce) {
        const Hit hit =
            TraceClosest(ray, frame.accelerationStructure, g_path.instanceBuffer, g_path.materialBuffer, kMaskAll);
        if (!hit.found) {
            // The sky, or the background a test chose when there is none.
            radiance += throughput * (frame.skyTexture != kNoTexture ? SkyRadiance(frame, ray.Direction) : g_path.background);
            break;
        }
        const Surface surface = ShadeHit(hit, ray.Direction, frame, g_path.instanceBuffer, g_path.materialBuffer);
        const float3 view = -ray.Direction;
        if (surface.transmission > 0.0) {
            // Smooth glass (ADR-0042): reflected or let through, as Fresnel says. Light that crossed thick
            // glass loses some of each color on the way; what goes in is tinted by the base color, and only
            // the transmission's share of it goes through. Lights shine through glass, unshaded, as their
            // shadow rays go through it in the mesh pass.
            if (bounce + 1 >= g_path.maxBounces) {
                break;
            }
            if (!surface.entering && !surface.thin) {
                throughput *= exp(-surface.absorption * hit.distance);
            }
            const float eta = surface.thin || surface.entering ? 1.0 / surface.ior : surface.ior;
            const float reflected = FresnelDielectric(saturate(dot(surface.normal, view)), eta);
            if (Random(random) < reflected) {
                ray.Direction = reflect(ray.Direction, surface.normal);
                ray.Origin = OffsetOrigin(surface.position, surface.faceNormal);
            } else {
                if (surface.thin || surface.entering) {
                    throughput *= surface.tint * surface.transmission;
                }
                if (!surface.thin) {
                    ray.Direction = refract(ray.Direction, surface.normal, eta);
                }
                ray.Origin = OffsetOrigin(surface.position, -surface.faceNormal);
            }
            ray.TMin = 0.0;
            ray.TMax = 1e6;
            continue;
        }
        radiance += throughput * (surface.emission + DirectLight(surface, view, frame, random));
        if (bounce + 1 >= g_path.maxBounces) {
            break;
        }
        const BsdfSample next = SampleBsdf(surface.normal, view, surface.diffuseColor, surface.f0, surface.roughness,
                                           float3(Random(random), Random(random), Random(random)));
        // A direction below the surface's face goes nowhere.
        if (all(next.weight <= 0.0) || dot(next.direction, surface.faceNormal) <= 0.0) {
            break;
        }
        throughput *= next.weight;
        // Russian roulette: dim paths end early, and the ones that go on count for those that stopped.
        if (bounce >= 2) {
            const float keep = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (Random(random) >= keep) {
                break;
            }
            throughput /= keep;
        }
        ray.Origin = OffsetOrigin(surface.position, surface.faceNormal);
        ray.Direction = next.direction;
        ray.TMin = 0.0;
        ray.TMax = 1e6;
    }

    const float4 previous = g_storageImages[g_path.output][id.xy];
    const float3 average =
        g_path.sampleIndex == 0 ? radiance : lerp(previous.rgb, radiance, 1.0 / float(g_path.sampleIndex + 1));
    g_storageImages[g_path.output][id.xy] = float4(average, 1.0);
}
