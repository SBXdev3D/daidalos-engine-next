// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Forward shading of opaque meshes, in linear HDR (ADR-0022): metallic-roughness PBR with normal maps,
// the frame's lights, ambient light, occlusion, emission and distance fog. A pixel evaluates only the
// lights of its cluster (ADR-0008). Up to four spot lights have a shadow map (ADR-0023).
// The ambient light is darkened by the frame's ambient occlusion (ADR-0024).

#include "MeshCommon.hlsli"

// The cluster of a pixel: its tile of the screen, and its slice of depth (ADR-0008).
uint ClusterOf(FrameData frame, float2 pixel, float3 worldPosition) {
    const float2 tile = pixel * frame.screenTexel * float2(kClusterColumns, kClusterRows);
    const uint column = min(uint(tile.x), kClusterColumns - 1);
    const uint row = min(uint(tile.y), kClusterRows - 1);
    const float depth = max(dot(worldPosition - frame.cameraPosition, frame.cameraForward), frame.nearPlane);
    const uint slice = min(uint(max(log(depth / frame.nearPlane) * frame.clusterSliceScale, 0.0)), kClusterSlices - 1);
    return column + row * kClusterColumns + slice * kClusterColumns * kClusterRows;
}

// How much of a light reaches a point: full up to fullRange, then fading linearly to zero at the range,
// and inside the cone of a spot light.
float Attenuation(LightData light, float distanceToLight, float3 toLight) {
    const float distance = saturate((light.range - distanceToLight) / max(light.range - light.fullRange, 0.0001));
    const float cone = smoothstep(light.cosOuter, light.cosInner, dot(-toLight, light.direction));
    return distance * cone;
}

// How much of a light's shadow map sees a point: 1 in full light, 0 in shadow. The point is pushed along
// its normal, more so far from the light, where a texel covers more surface. Nine filtered comparisons
// soften the edge over about three texels.
float ShadowFactor(FrameData frame, LightData light, float3 position, float3 normal, float distanceToLight) {
    const ShadowData shadow = g_shadows[frame.shadowBuffer][light.shadow];
    const float3 receiver = position + normal * (shadow.normalOffset * distanceToLight);
    const float4 clip = mul(shadow.viewProjection, float4(receiver, 1.0));
    if (clip.w <= 0.0) {
        return 1.0;
    }
    const float3 ndc = clip.xyz / clip.w;
    // Clip-space Y points up, and texture rows go down.
    const float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return 1.0;
    }
    // The taps stay inside the light's tile of the atlas.
    const float texel = frame.shadowTexel;
    const float2 tileMin = shadow.atlasOffset + 1.5 * texel;
    const float2 tileMax = shadow.atlasOffset + shadow.atlasScale - 1.5 * texel;
    const float2 center = clamp(shadow.atlasOffset + uv * shadow.atlasScale, tileMin, tileMax);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += g_textures[frame.shadowAtlas].SampleCmpLevelZero(g_compareSamplers[kSamplerShadowCompare],
                                                                    center + float2(x, y) * texel, ndc.z);
        }
    }
    return lit / 9.0;
}

// Cook-Torrance with the GGX distribution, height-correlated Smith visibility and Schlick's Fresnel,
// over a Lambert diffuse. The result is multiplied by pi, so that a light's color is what a white
// surface facing it reflects (ADR-0022).
float3 Shade(float3 normal, float3 view, float3 toLight, float3 diffuseColor, float3 f0, float roughness) {
    const float nDotL = saturate(dot(normal, toLight));
    if (nDotL <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 halfway = normalize(view + toLight);
    const float nDotV = max(dot(normal, view), 0.0001);
    const float nDotH = saturate(dot(normal, halfway));
    const float vDotH = saturate(dot(view, halfway));

    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kPi * d * d);
    const float visibility =
        0.5 / (nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2) +
               nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2));
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);

    const float3 specular = distribution * visibility * fresnel;
    const float3 diffuse = (1.0 - fresnel) * diffuseColor / kPi;
    return (diffuse + specular) * kPi * nDotL;
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
    const uint2 cluster = g_clusters[frame.clusterBuffer][ClusterOf(frame, input.clipPosition.xy, input.worldPosition)];
    for (uint entry = 0; entry < cluster.y; ++entry) {
        const LightData light = g_lights[frame.lightBuffer][g_clusterLights[frame.clusterLightBuffer][cluster.x + entry]];
        const float3 offset = light.position - input.worldPosition;
        const float distanceToLight = length(offset);
        if (distanceToLight >= light.range) {
            continue;
        }
        const float3 toLight = offset / max(distanceToLight, 0.0001);
        float attenuation = Attenuation(light, distanceToLight, toLight);
        if (attenuation > 0.0 && light.shadow != kNoShadow) {
            attenuation *= ShadowFactor(frame, light, input.worldPosition, normal, distanceToLight);
        }
        if (attenuation > 0.0) {
            color += light.color * attenuation * Shade(normal, view, toLight, diffuseColor, f0, roughness);
        }
    }

    const float visibility = exp(-frame.fogDensity * cameraDistance);
    return float4(lerp(frame.fogColor, color, visibility), 1.0);
}
