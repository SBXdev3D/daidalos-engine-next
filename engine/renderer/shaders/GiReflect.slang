// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The light each pixel's specular lobe reflects (ADR-0042): one ray from the G-buffer's surface, in a
// direction drawn from the visible normals of its GGX lobe (SampleSpecularDirection), and the light that
// comes back along it. Averaged, and multiplied by the surface's specular albedo, rays in that distribution
// give its specular reflection; the lighting pass does the multiplying, and the denoiser the averaging.
//
// What comes back:
// - On a smooth surface, sharp enough to show what it reflects: what the ray hits, shaded with its whole
//   BSDF, so that metals show: its emission, the light of one light chosen among a few, with a shadow ray,
//   and the indirect light that reaches it, from one more ray into the cache, for its diffuse and its
//   specular parts. A mirror-like hit reflects on instead, up to kMirrorBounces surfaces. Emission shows,
//   emissive lights included: the lighting of the pixel, and of a mirror that reflects on, leaves out their
//   specular light (ADR-0044).
// - On a rougher one, whose reflection is a blur: the light the cache holds where the ray hits. Many of
//   these hits are out of view, where no path of the cache goes: a cell worth too few samples gets one, the
//   hit shaded as above, so that reflections fill the cache where they need it (as SHaRC's paths do).
// - Past the sky: the sky.
// - Rougher than g_gi.maxRoughness: no ray; the pixel's indirect diffuse light stands in for it, as before.
// - Rough, with g_gi.spacing 2: half the pixels trace, in a checkerboard that takes turns, and GiReflectFill.slang
//   fills the others (ADR-0096). The pass runs twice, a thread per pixel of each half: the half whose turn it is
//   traces every reflection; the other, g_gi.sharpOnly, only sharp ones, so that its groups on rough surfaces
//   end at once. Skipping pixels within a group would save nothing: the group lasts as long as its slowest.
// Alpha is the distance to the first hit, which the denoiser follows the reflection with; 0 where the
// reflection moves with the surface.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "RayQueries.slangh"
#include "GlobalIllumination.slangh"
import Bsdf;

[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_storageImages[];

// Below this roughness, what a reflection ray hits is a mirror that reflects on.
static const float kMirrorRoughness = 0.1;
static const uint kMirrorBounces = 3;
// A cell worth fewer samples than this is shaded anew, and gets the result.
static const float kKnownSamples = 4.0;
// Distances beyond this are as good as infinite to the denoiser, and fit a half float.
static const float kFarHit = 1000.0;

// The indirect light that reaches a surface, per unit of diffuse color: one ray into the cache, as the
// indirect light pass traces it for pixels.
float3 IndirectAt(FrameData frame, Surface surface, inout uint random) {
    RayDesc ray;
    ray.Direction = CosineDirection(surface.normal, float2(Random(random), Random(random)));
    if (dot(ray.Direction, surface.faceNormal) <= 0.0) {
        ray.Direction = reflect(ray.Direction, surface.faceNormal);
    }
    ray.Origin = OffsetOrigin(surface.position, surface.faceNormal);
    ray.TMin = 0.0;
    ray.TMax = 1e6;
    const Hit hit = TraceClosest(ray, frame, g_gi.instanceBuffer, g_gi.materialBuffer, kMaskReflection);
    if (!hit.found) {
        return SkyRadiance(frame, ray.Direction);
    }
    if (hit.particle) {
        return hit.particleLight;
    }
    const InstanceData instance = g_instances[g_gi.instanceBuffer][hit.instance];
    float3 hitNormal = normalize(FaceNormal(instance, hit.primitive));
    if (dot(hitNormal, ray.Direction) > 0.0) {
        hitNormal = -hitNormal;
    }
    return CachedRadiance(frame, ray.Origin + ray.Direction * hit.distance, hitNormal);
}

// The light of the frame's lights that a surface sends towards view, with its whole BSDF: one light chosen
// among a few, the brighter the more likely, and one shadow ray (Lights.slangh). A mirror that reflects on
// leaves out the specular light of emissive lights, which the next ray shows.
float3 DirectLight(Surface surface, float3 view, FrameData frame, bool emissiveSpecular, inout uint random) {
    Receiver receiver = ReceiverOf(surface, view, kRespondBsdf);
    receiver.emissiveSpecular = emissiveSpecular;
    return SampleDirectLight(frame, receiver, random, g_gi.instanceBuffer, g_gi.materialBuffer);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const FrameData frame = g_frames[g_gi.frameBuffer][0];
    // With g_gi.spacing 2, a thread per pixel of one half of a checkerboard (ADR-0096).
    const int2 pixel = TracedPixel(id.xy);
    if (any(pixel >= int2(g_gi.width, g_gi.height))) {
        return;
    }
    const float depth = g_textures[g_gi.depth].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        g_storageImages[g_gi.reflections][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float4 normalRoughness = g_textures[g_gi.normals].Load(int3(pixel, 0));
    const float roughness = normalRoughness.a;
    if (roughness > g_gi.maxRoughness) {
        g_storageImages[g_gi.reflections][pixel] = float4(g_storageImages[g_gi.output][pixel].rgb, 0.0);
        return;
    }
    // The half whose turn it is not traces only sharp reflections (ADR-0096): GiReflectFill.slang fills the rough.
    if (roughness >= kSharpRoughness && g_gi.sharpOnly != 0) {
        g_storageImages[g_gi.reflections][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) * frame.screenTexel, depth);
    const float3 faceNormal = FaceNormalAt(frame, pixel, position);
    const float3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
    const float3 view = normalize(frame.cameraPosition - position);
    uint random = Pcg((uint(pixel.y) * g_gi.width + uint(pixel.x)) * 7919u + g_gi.seed * 104729u + 17u);

    RayDesc ray;
    ray.Direction = SampleSpecularDirection(normal, view, roughness, float2(Random(random), Random(random)));
    if (dot(ray.Direction, faceNormal) <= 0.0) {
        ray.Direction = reflect(ray.Direction, faceNormal);
    }
    // A position from depth is less precise than a hit's: farther off the surface, the farther it is.
    ray.Origin = OffsetOrigin(position, faceNormal) + faceNormal * (0.002 * distance(position, frame.cameraPosition));
    ray.TMin = 0.0;
    ray.TMax = 1e6;

    const bool sharp = roughness < kSharpRoughness;
    float3 light = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    float hitDistance = kFarHit;
    for (uint bounce = 0; bounce < kMirrorBounces; ++bounce) {
        const Hit hit = TraceClosest(ray, frame, g_gi.instanceBuffer, g_gi.materialBuffer, kMaskReflection);
        if (!hit.found) {
            light += throughput * SkyRadiance(frame, ray.Direction);
            break;
        }
        if (bounce == 0) {
            hitDistance = min(hit.distance, kFarHit);
        }
        if (hit.particle) {
            // A particle in the mirror: the light it sends back, which the cache must not keep (ADR-0062).
            light += throughput * hit.particleLight;
            break;
        }
        const Surface surface = ShadeHit(hit, ray.Direction, frame, g_gi.instanceBuffer, g_gi.materialBuffer);
        if (!sharp) {
            const uint cell = FindCellOf(frame, surface.position, surface.faceNormal, true);
            const float4 cached = cell != kNoCell ? g_cacheRadiance[g_gi.radiance][cell] : float4(0.0, 0.0, 0.0, 0.0);
            if (cached.a >= kKnownSamples) {
                light += throughput * cached.rgb;
            } else {
                // Emission adds nothing to the cache (ADR-0044).
                const float3 sent =
                    DirectDiffuseLight(surface, frame, random) + surface.diffuseColor * IndirectAt(frame, surface, random);
                AddToCell(cell, sent);
                light += throughput * sent;
            }
            break;
        }
        const float3 hitView = -ray.Direction;
        const float3 specularAlbedo = SpecularAlbedo(surface.f0, surface.roughness, max(dot(surface.normal, hitView), 0.0));
        const bool mirror = surface.roughness < kMirrorRoughness;
        // A mirror's specular indirect light is what it reflects on: the next bounce.
        light += throughput * (surface.emission + DirectLight(surface, hitView, frame, !mirror, random) +
                               (surface.diffuseColor + (mirror ? 0.0 : specularAlbedo)) *
                                   IndirectAt(frame, surface, random));
        if (!mirror) {
            break;
        }
        // A mirror: what it reflects, as much as it reflects.
        throughput *= specularAlbedo;
        if (all(throughput < 0.01)) {
            break;
        }
        ray.Direction = reflect(ray.Direction, surface.normal);
        if (dot(ray.Direction, surface.faceNormal) <= 0.0) {
            break;
        }
        ray.Origin = OffsetOrigin(surface.position, surface.faceNormal);
    }
    g_storageImages[g_gi.reflections][pixel] = float4(light, hitDistance);
}
