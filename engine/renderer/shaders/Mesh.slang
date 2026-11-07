// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Forward shading of opaque meshes, in linear HDR (ADR-0022): metallic-roughness PBR with normal maps,
// the frame's lights, ambient light, occlusion, emission and distance fog. A pixel evaluates only the
// lights of its cluster (ADR-0008). Every light casts shadows: a ray towards it says whether it reaches
// the pixel (ADR-0035).
// The ambient light is darkened by the frame's ambient occlusion (ADR-0024).

#include "MeshCommon.slangh"
#include "RayQueries.slangh"
import Bsdf;

// The BSDF of the engine (Bsdf.slang), multiplied by pi, so that a light's color is what a white surface
// facing it reflects (ADR-0022).
float3 Shade(float3 normal, float3 view, float3 toLight, float3 diffuseColor, float3 f0, float roughness) {
    return EvaluateBsdf(normal, view, toLight, diffuseColor, f0, roughness) * kPi;
}

// The specular part of light that comes from everywhere, without an environment to reflect: Karis's
// analytic fit of the split-sum lookup, so that metals are not black under ambient light.
float3 AmbientSpecular(float3 f0, float roughness, float nDotV) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
}

float4 PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];

    const float4 baseColor = BaseColor(material, input.uv);
    // glTF: roughness in green, metalness in blue.
    const float4 metallicRoughness = SampleTexture(material.metallicRoughnessTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0));
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    const float occlusion =
        lerp(1.0, SampleTexture(material.occlusionTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).r, material.occlusionStrength);
    const float3 emission =
        material.emissiveFactor * SampleTexture(material.emissiveTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).rgb;

    const float3 normal = SurfaceNormal(input, isFrontFace, material, frame.normalStrength);

    const float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    const float3 toCamera = frame.cameraPosition - input.worldPosition;
    const float cameraDistance = length(toCamera);
    const float3 view = toCamera / max(cameraDistance, 0.0001);

    const float3 ambientSpecular = AmbientSpecular(f0, roughness, max(dot(normal, view), 0.0));
    // Light from everywhere, brighter from above than from below: what leans up catches more of it. Only
    // this light is occluded: direct light has its shadows.
    float ambientOcclusion = occlusion;
    if (frame.occlusionTexture != kNoTexture) {
        ambientOcclusion *= g_textures[frame.occlusionTexture]
                                .SampleLevel(g_samplers[kSamplerLinearClamp], input.clipPosition.xy * frame.screenTexel, 0.0)
                                .r;
    }
    const float3 ambient = lerp(frame.ambientBelow, frame.ambient, normal.y * 0.5 + 0.5);
    float3 color = ambient * (diffuseColor + ambientSpecular) * ambientOcclusion + emission;
    // The face's own normal, towards the camera, from which shadow rays start.
    float3 faceNormal = normalize(cross(ddy(input.worldPosition), ddx(input.worldPosition)));
    if (dot(faceNormal, view) < 0.0) {
        faceNormal = -faceNormal;
    }
    const uint2 cluster = g_clusters[frame.clusterBuffer][ClusterOf(frame, input.clipPosition.xy, input.worldPosition)];
    for (uint entry = 0; entry < cluster.y; ++entry) {
        const LightData light = g_lights[frame.lightBuffer][g_clusterLights[frame.clusterLightBuffer][cluster.x + entry]];
        const float3 offset = light.position - input.worldPosition;
        const float distanceToLight = length(offset);
        if (distanceToLight >= light.range) {
            continue;
        }
        const float3 toLight = offset / max(distanceToLight, 0.0001);
        const float attenuation = Attenuation(light, distanceToLight, toLight);
        if (attenuation <= 0.0) {
            continue;
        }
        const float3 reflected = light.color * attenuation * Shade(normal, view, toLight, diffuseColor, f0, roughness);
        // Only light that would add something is worth a ray.
        if (any(reflected > 0.0) &&
            LightReaches(input.worldPosition, faceNormal, toLight, distanceToLight, light.range,
                         frame.accelerationStructure, g_push.instanceBuffer, g_push.materialBuffer)) {
            color += reflected;
        }
    }

    const float visibility = exp(-frame.fogDensity * cameraDistance);
    return float4(lerp(frame.fogColor, color, visibility), 1.0);
}
