// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The reference path tracer (ADR-0034): one path per pixel per frame, averaged with the paths of earlier
// frames until the view changes. It shades hits with the materials and lights of the mesh pass and its
// BSDF (Bsdf.slang): it is the image that real-time techniques are compared with. It has no fog and no
// ambient term: light bounces instead. A path that leaves the scene sees the background radiance.

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

// The surface at a hit, as the mesh pass shades it: its material, read at full detail, and its normal,
// with the normal map, turned towards the ray as the mesh pass does for the back of double-sided surfaces.
struct Surface {
    float3 position;
    float3 faceNormal;
    float3 normal;
    float3 diffuseColor;
    float3 f0;
    float roughness;
    float3 emission;
};

Surface ShadeHit(Hit hit, float3 rayDirection, FrameData frame) {
    const InstanceData instance = g_instances[g_path.instanceBuffer][hit.instance];
    const MaterialData material = g_materials[g_path.materialBuffer][instance.material];
    const Vertex vertex = Interpolate(instance, hit.primitive, hit.barycentrics);

    Surface surface;
    surface.position = mul(instance.world, float4(vertex.position, 1.0)).xyz;
    surface.faceNormal = normalize(FaceNormal(instance, hit.primitive));
    float3 normal = normalize(TransformNormal(instance.world, vertex.normal));
    float4 tangent = float4(mul((float3x3)instance.world, vertex.tangent.xyz), vertex.tangent.w);
    if (dot(surface.faceNormal, rayDirection) > 0.0) {
        surface.faceNormal = -surface.faceNormal;
        normal = -normal;
        tangent.w = -tangent.w;
    }
    if (material.normalTexture != kNoTexture && dot(tangent.xyz, tangent.xyz) > 0.0) {
        const float3 t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
        const float3 b = cross(normal, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
        float2 xy = SampleLevelZero(material.normalTexture, vertex.uv, float4(0.5, 0.5, 1.0, 1.0)).rg * 2.0 - 1.0;
        xy *= material.normalScale * frame.normalStrength;
        const float z = sqrt(saturate(1.0 - dot(xy, xy)));
        normal = normalize(t * xy.x + b * xy.y + normal * z);
    }
    surface.normal = normal;

    const float4 baseColor = material.baseColorFactor * SampleLevelZero(material.baseColorTexture, vertex.uv, float4(1.0, 1.0, 1.0, 1.0));
    // glTF: roughness in green, metalness in blue.
    const float4 metallicRoughness = SampleLevelZero(material.metallicRoughnessTexture, vertex.uv, float4(1.0, 1.0, 1.0, 1.0));
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    surface.roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    surface.diffuseColor = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    surface.emission = material.emissiveFactor * SampleLevelZero(material.emissiveTexture, vertex.uv, float4(1.0, 1.0, 1.0, 1.0)).rgb;
    return surface;
}

// Light that reaches the surface straight from the frame's lights and leaves towards view, with the
// mesh pass's falloff and its convention that a light's color is what a white surface facing it reflects.
float3 DirectLight(Surface surface, float3 view, FrameData frame) {
    float3 total = float3(0.0, 0.0, 0.0);
    for (uint index = 0; index < frame.lightCount; ++index) {
        const LightData light = g_lights[frame.lightBuffer][index];
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
        const float3 reflected = light.color * attenuation * kPi *
                                 EvaluateBsdf(surface.normal, view, toLight, surface.diffuseColor, surface.f0, surface.roughness);
        if (all(reflected <= 0.0)) {
            continue;
        }
        if (LightReaches(surface.position, surface.faceNormal, toLight, distanceToLight, light.range,
                         frame.accelerationStructure, g_path.instanceBuffer, g_path.materialBuffer)) {
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
        const Hit hit = TraceClosest(ray, frame.accelerationStructure, g_path.instanceBuffer, g_path.materialBuffer);
        if (!hit.found) {
            // The sky, or the background a test chose when there is none.
            radiance += throughput * (frame.skyTexture != kNoTexture ? SkyRadiance(frame, ray.Direction) : g_path.background);
            break;
        }
        const Surface surface = ShadeHit(hit, ray.Direction, frame);
        const float3 view = -ray.Direction;
        radiance += throughput * (surface.emission + DirectLight(surface, view, frame));
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
