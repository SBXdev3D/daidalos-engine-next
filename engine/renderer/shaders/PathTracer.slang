// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The reference path tracer (ADR-0034): one path per pixel per frame, averaged with the paths of earlier
// frames until the view changes. It shades hits with the materials and lights of the mesh pass and its
// BSDF (Bsdf.slang): it is the image that real-time techniques are compared with. It has no fog and no
// ambient term: light bounces instead. A path that leaves the scene sees the background radiance.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
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
    uint padding0;
    uint padding1;
    uint padding2;
    float3 background;
    float padding3;
};

[[vk::push_constant]] PathTracerPushConstants g_path;
[[vk::binding(2, 0)]] StructuredBuffer<float> g_vertexFloats[];
[[vk::binding(2, 0)]] StructuredBuffer<uint> g_indices[];
[[vk::binding(3, 0)]] RaytracingAccelerationStructure g_accelerationStructures[];
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_storageImages[];

static const float kPi = 3.14159265;
// Matches kMaskShadowCasters in renderer/src/RayTracingScene.hpp.
static const uint kMaskShadowCasters = 0x01;
// Nothing this close to a light shadows it: a lamp's own shade, around the light, would darken it. The
// shadow maps of the mesh pass do the same (kShadowNearPlane in renderer/src/MeshPass.cpp, ADR-0023).
static const float kShadowNearPlane = 0.1;

// PCG (O'Neill 2014): a small, good random number generator.
uint NextRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint state) {
    return float(NextRandom(state) >> 8) * (1.0 / 16777216.0);
}

struct Vertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

Vertex LoadVertex(InstanceData instance, uint index) {
    const uint first = (instance.firstVertex + index) * kVertexFloats;
    const uint buffer = instance.vertexBuffer;
    Vertex vertex;
    vertex.position = float3(g_vertexFloats[buffer][first], g_vertexFloats[buffer][first + 1], g_vertexFloats[buffer][first + 2]);
    vertex.normal = float3(g_vertexFloats[buffer][first + 3], g_vertexFloats[buffer][first + 4], g_vertexFloats[buffer][first + 5]);
    vertex.tangent = float4(g_vertexFloats[buffer][first + 6], g_vertexFloats[buffer][first + 7], g_vertexFloats[buffer][first + 8],
                            g_vertexFloats[buffer][first + 9]);
    vertex.uv = float2(g_vertexFloats[buffer][first + 10], g_vertexFloats[buffer][first + 11]);
    return vertex;
}

// The three vertices of a triangle, blended by the barycentric coordinates of a hit.
Vertex Interpolate(InstanceData instance, uint primitive, float2 barycentrics) {
    const uint first = primitive * 3;
    const Vertex a = LoadVertex(instance, g_indices[instance.indexBuffer][first]);
    const Vertex b = LoadVertex(instance, g_indices[instance.indexBuffer][first + 1]);
    const Vertex c = LoadVertex(instance, g_indices[instance.indexBuffer][first + 2]);
    const float3 w = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    Vertex result;
    result.position = a.position * w.x + b.position * w.y + c.position * w.z;
    result.normal = a.normal * w.x + b.normal * w.y + c.normal * w.z;
    result.tangent = a.tangent * w.x + b.tangent * w.y + c.tangent * w.z;
    result.uv = a.uv * w.x + b.uv * w.y + c.uv * w.z;
    // The handedness of the tangent frame is a sign, not something to blend.
    result.tangent.w = a.tangent.w;
    return result;
}

float3 FaceNormal(InstanceData instance, uint primitive) {
    const uint first = primitive * 3;
    const float3 a = LoadVertex(instance, g_indices[instance.indexBuffer][first]).position;
    const float3 b = LoadVertex(instance, g_indices[instance.indexBuffer][first + 1]).position;
    const float3 c = LoadVertex(instance, g_indices[instance.indexBuffer][first + 2]).position;
    return TransformNormal(instance.world, cross(b - a, c - a));
}

// Rays have no screen derivatives: textures are read at their full detail.
float4 SampleLevelZero(uint texture, float2 uv, float4 fallback) {
    return texture != kNoTexture ? g_textures[texture].SampleLevel(g_samplers[kSamplerLinearRepeat], uv, 0.0) : fallback;
}

// The alpha test of the mesh pass, for a candidate hit on a surface that is not opaque.
bool PassesAlphaTest(uint instanceIndex, uint primitive, float2 barycentrics) {
    const InstanceData instance = g_instances[g_path.instanceBuffer][instanceIndex];
    const MaterialData material = g_materials[g_path.materialBuffer][instance.material];
    const float2 uv = Interpolate(instance, primitive, barycentrics).uv;
    const float alpha = material.baseColorFactor.a * SampleLevelZero(material.baseColorTexture, uv, float4(1.0, 1.0, 1.0, 1.0)).a;
    return alpha >= material.alphaCutoff;
}

// The first surface a ray meets, alpha-tested surfaces included.
struct Hit {
    bool found;
    uint instance;
    uint primitive;
    float2 barycentrics;
    float distance;
};

Hit TraceClosest(RayDesc ray, uint structure) {
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_accelerationStructures[structure], RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            PassesAlphaTest(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(),
                            query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    Hit hit;
    hit.found = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    hit.instance = query.CommittedInstanceID();
    hit.primitive = query.CommittedPrimitiveIndex();
    hit.barycentrics = query.CommittedTriangleBarycentrics();
    hit.distance = query.CommittedRayT();
    return hit;
}

// Whether anything that casts shadows lies on the ray.
bool Occluded(RayDesc ray, uint structure) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_accelerationStructures[structure], RAY_FLAG_NONE, kMaskShadowCasters, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            PassesAlphaTest(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(),
                            query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Moves a ray's origin off the surface it starts from, so that it does not hit it again.
float3 OffsetOrigin(float3 position, float3 faceNormal) {
    return position + faceNormal * (1e-3 * (1.0 + max(abs(position.x), max(abs(position.y), abs(position.z))) * 0.01));
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
        RayDesc shadow;
        shadow.Origin = OffsetOrigin(surface.position, surface.faceNormal);
        shadow.Direction = toLight;
        shadow.TMin = 0.0;
        shadow.TMax = max(distanceToLight - min(kShadowNearPlane, light.range * 0.5), 0.0);
        if (!Occluded(shadow, frame.accelerationStructure)) {
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
    const float2 uv = (float2(id.xy) + float2(Random(random), Random(random))) / float2(size);
    const float4 through = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
    RayDesc ray;
    ray.Origin = frame.cameraPosition;
    ray.Direction = normalize(through.xyz / through.w - frame.cameraPosition);
    ray.TMin = frame.nearPlane;
    ray.TMax = 1e6;

    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    for (uint bounce = 0; bounce < g_path.maxBounces; ++bounce) {
        const Hit hit = TraceClosest(ray, frame.accelerationStructure);
        if (!hit.found) {
            radiance += throughput * g_path.background;
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
