// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The reference path tracer (ADR-0034): one path per pixel per frame, averaged with the paths of earlier
// frames until the view changes. It shades hits with the materials and lights of the G-buffer and its
// BSDF (Bsdf.slang): it is the image that real-time techniques are compared with. A path that leaves the
// scene sees the background radiance. Fog is a participating medium (ADR-0051) that paths cross: where
// delta tracking finds that light scatters, the path samples the lights and goes on in a direction the
// phase function draws; shadow rays lose what ratio tracking says the fog takes, the boxes' included.
// Analytic lights are counted by sampling them at each surface, emissive surfaces only when a ray hits
// them: each light is counted by one strategy. Emission that is not a light is only seen, as the engine
// shows it (ADR-0044): by the camera, through glass, and in sharp reflections.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "Random.slangh"
#include "Fog.slangh"
import Bsdf;

// Smoother surfaces show emission that is not a light in their reflections, as the engine's sharp
// reflections do. Matches GlobalIllumination::kSharpRoughness.
static const float kSharpReflection = 0.3;

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
    uint directOnly; // 1: lights are sampled at the first surface only, and emission is all that counts after.
    uint padding2;
    float3 background;
    float padding3;
};

[[vk::push_constant]] PathTracerPushConstants g_path;
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];

static const float kPi = 3.14159265;

// Steps of delta and ratio tracking along one ray, at most: beyond them, the medium is taken as clear.
static const uint kMaxFogSteps = 256;

// The densest the fog gets anywhere: the level's, plus every box's densest.
float FogMajorant(FrameData frame) {
    float majorant = frame.fogDensity;
    for (uint index = 0; index < frame.fogVolumeCount; ++index) {
        majorant += FogVolumeMajorant(g_fogVolumes[frame.fogVolumeBuffer][index]);
    }
    return majorant;
}

// Where along a ray the fog may be: within the view distance of the camera, between start and end.
float2 FogInterval(FrameData frame, float3 origin, float3 direction, float start, float end) {
    const float3 offset = origin - frame.cameraPosition;
    const float b = dot(offset, direction);
    const float c = dot(offset, offset) - frame.farPlane * frame.farPlane;
    const float discriminant = b * b - c;
    if (discriminant <= 0.0) {
        return float2(1.0, 0.0);
    }
    const float root = sqrt(discriminant);
    return float2(max(-b - root, start), min(-b + root, end));
}

// Delta tracking (Woodcock et al. 1965): the distance along the ray at which light scatters or is absorbed
// in the fog, drawn in proportion to the fog's transmittance, or a negative value if it crosses to end.
float FogCollision(FrameData frame, float3 origin, float3 direction, float start, float end, float majorant,
                   inout uint random) {
    const float2 interval = FogInterval(frame, origin, direction, start, end);
    float t = interval.x;
    for (uint step = 0; step < kMaxFogSteps && majorant > 0.0; ++step) {
        t -= log(1.0 - Random(random)) / majorant;
        if (t >= interval.y) {
            break;
        }
        if (Random(random) * majorant < FogMediumAt(frame, origin + direction * t).extinction) {
            return t;
        }
    }
    return -1.0;
}

// Ratio tracking (Novak et al. 2014): an unbiased estimate of the fraction of the light that crosses the fog
// along the ray, from start to end.
float FogTransmittance(FrameData frame, float3 origin, float3 direction, float start, float end, float majorant,
                       inout uint random) {
    if (majorant <= 0.0) {
        return 1.0;
    }
    const float2 interval = FogInterval(frame, origin, direction, start, end);
    float t = interval.x;
    float transmittance = 1.0;
    for (uint step = 0; step < kMaxFogSteps; ++step) {
        t -= log(1.0 - Random(random)) / majorant;
        if (t >= interval.y) {
            break;
        }
        transmittance *= 1.0 - FogMediumAt(frame, origin + direction * t).extinction / majorant;
    }
    return max(transmittance, 0.0);
}

// Light that reaches the surface straight from the frame's lights and leaves towards view: their
// illuminance, with the lighting's falloff (ADR-0043), times the BSDF.
// As the lighting does, it shades with the light's center and aims its shadow ray at a random point of the
// light's sphere (ADR-0039).
float3 DirectLight(Surface surface, float3 view, FrameData frame, float majorant, inout uint random) {
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
                                 EvaluateLayeredBsdf(surface.normal, view, toLight, surface.diffuseColor, surface.f0,
                                                     surface.roughness, surface.layers);
        if (all(reflected <= 0.0)) {
            continue;
        }
        const float2 u = float2(Random(random), Random(random));
        const float3 toPoint = SampleLightPoint(light.position, light.radius, surface.position, u) - surface.position;
        const float distanceToPoint = length(toPoint);
        const float3 direction = toPoint / max(distanceToPoint, 0.0001);
        if (LightReaches(surface.position, surface.faceNormal, direction, distanceToPoint, light.range,
                         frame, g_path.instanceBuffer, g_path.materialBuffer)) {
            total += reflected * FogTransmittance(frame, surface.position, direction, 0.0, distanceToPoint, majorant, random);
        }
    }
    return total;
}

// Light that reaches a point in the fog straight from the frame's lights and scatters along a ray going in
// direction, per unit of the fog's scattering: their illuminance times the phase function.
float3 FogDirectLight(float3 position, float3 direction, FrameData frame, float majorant, inout uint random) {
    float3 total = float3(0.0, 0.0, 0.0);
    for (uint index = 0; index < frame.lightCount; ++index) {
        const LightData light = LightAt(g_lights[frame.lightBuffer][index], position);
        const float3 offset = light.position - position;
        const float distanceToLight = length(offset);
        if (distanceToLight >= light.range) {
            continue;
        }
        const float3 toLight = offset / max(distanceToLight, 0.0001);
        const float attenuation = Attenuation(light, distanceToLight, toLight);
        if (attenuation <= 0.0) {
            continue;
        }
        const float3 scattered = light.color * attenuation * FogPhase(frame.fogAnisotropy, dot(toLight, direction));
        const float2 u = float2(Random(random), Random(random));
        const float3 toPoint = SampleLightPoint(light.position, light.radius, position, u) - position;
        const float distanceToPoint = length(toPoint);
        const float3 towards = toPoint / max(distanceToPoint, 0.0001);
        if (LightReaches(position, towards, towards, distanceToPoint, light.range, frame,
                         g_path.instanceBuffer, g_path.materialBuffer)) {
            total += scattered * FogTransmittance(frame, position, towards, 0.0, distanceToPoint, majorant, random);
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

    const float majorant = FogMajorant(frame);
    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    // The path came straight from the camera, through glass or off a sharp reflection: it sees emission that
    // is not a light.
    bool seen = true;
    // What the next ray sees (ADR-0059): the camera's, then a sharp reflection's or bounced light's.
    uint mask = kMaskCamera | kMaskGlass;
    for (uint bounce = 0; bounce < g_path.maxBounces; ++bounce) {
        const Hit hit =
            TraceClosest(ray, frame, g_path.instanceBuffer, g_path.materialBuffer, mask);
        // The fog in front of what the ray hits may scatter or absorb the light first.
        const float collision = FogCollision(frame, ray.Origin, ray.Direction, ray.TMin,
                                             hit.found ? hit.distance : ray.TMax, majorant, random);
        if (collision >= 0.0) {
            const float3 position = ray.Origin + ray.Direction * collision;
            const FogMedium medium = FogMediumAt(frame, position);
            // A glowing fog gives off light where the path meets it: delta tracking stops in proportion to
            // the extinction, so what it gives off per meter counts divided by it.
            radiance += throughput * medium.emission / max(medium.extinction, 1e-6);
            // Absorbed or scattered, as the albedo says, per color.
            throughput *= medium.scattering / max(medium.extinction, 1e-6);
            if (g_path.directOnly == 0 || bounce == 0) {
                radiance += throughput * FogDirectLight(position, ray.Direction, frame, majorant, random);
            }
            if (bounce + 1 >= g_path.maxBounces) {
                break;
            }
            // The phase function is drawn exactly: the throughput keeps its value.
            ray.Origin = position;
            ray.Direction = SampleFogPhase(frame.fogAnisotropy, ray.Direction, float2(Random(random), Random(random)));
            ray.TMin = 0.0;
            ray.TMax = 1e6;
            seen = false;
            continue;
        }
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
            // shadow rays go through it in the lighting.
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
        if (seen || surface.emissiveLight) {
            radiance += throughput * surface.emission;
        }
        if (g_path.directOnly == 0 || bounce == 0) {
            radiance += throughput * DirectLight(surface, view, frame, majorant, random);
        }
        if (bounce + 1 >= g_path.maxBounces) {
            break;
        }
        const BsdfSample next =
            SampleLayeredBsdf(surface.normal, view, surface.diffuseColor, surface.f0, surface.roughness, surface.layers,
                              float4(Random(random), Random(random), Random(random), Random(random)));
        // A direction below the surface's face goes nowhere.
        if (all(next.weight <= 0.0) || dot(next.direction, surface.faceNormal) <= 0.0) {
            break;
        }
        throughput *= next.weight;
        seen = surface.roughness < kSharpReflection;
        mask = (seen ? kMaskReflection : kMaskIndirect) | kMaskGlass;
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
