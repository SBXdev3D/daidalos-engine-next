// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Forward shading of opaque meshes, in linear HDR (ADR-0022): metallic-roughness PBR with normal maps,
// the frame's lights, its indirect light, occlusion and emission. A pixel evaluates only the lights of its
// cluster (ADR-0008). Every light casts shadows: a ray towards a random point of the light says whether
// it reaches the pixel, which makes the shadows of lights with a radius soft, and noisy (ADR-0035,
// ADR-0039).
// The indirect light comes from the global illumination pass, one ray per pixel (ADR-0040): noisy too. The
// specular lobe reflects what its own ray found (ADR-0042); without reflections, the indirect light stands in
// for it, as if every direction sent the same light.
// The pass writes the light apart from the emission, diffuse and specular, without the colors of the
// surface, for the denoiser; the composite pass puts them back together and adds the fog.

#include "MeshCommon.slangh"
#include "RayQueries.slangh"
import Bsdf;

// What the pass writes for each pixel (ADR-0039).
struct ShadingOutput {
    // Emission, in linear HDR.
    float4 base : SV_Target0;
    // The diffuse light per unit of diffuse color. Alpha is 1 where it may be noisy: where there is indirect
    // light, or a shadow ray aimed at a random point of a light, so that the denoiser filters it.
    float4 diffuse : SV_Target1;
    // The specular light divided by the surface's specular albedo, and the albedo in alpha.
    float4 specular : SV_Target2;
    // The diffuse color, which the composite pass multiplies the diffuse light by.
    float4 albedo : SV_Target3;
};

// A number from 0 to 1 per pixel that looks like noise, but spreads evenly over neighbouring pixels
// (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", 2014).
float InterleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// Two numbers from 0 to 1 for a pixel's ray towards a light: over frames, the R2 sequence, which covers the
// square evenly whatever the number of frames (Roberts 2018); over pixels, shifted by noise that spreads
// evenly over neighbours. Each light of the pixel is shifted further, so that they do not share points.
float2 LightSample(float2 pixel, uint frame, uint light) {
    const float2 shift = float2(InterleavedGradientNoise(pixel), InterleavedGradientNoise(pixel.yx + 17.0));
    return frac(shift + float(frame % 4096) * float2(0.7548776662, 0.5698402910) +
                float(light) * float2(0.618034, 0.414214));
}

ShadingOutput PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];

    const float4 baseColor = BaseColor(material, input.uv);
    // glTF: roughness in green, metalness in blue.
    const float4 metallicRoughness = SampleTexture(material.metallicRoughnessTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0));
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    const float occlusion =
        lerp(1.0, SampleTexture(material.occlusionTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).r, material.occlusionStrength);
    // Nits, times the frame's exposure (ADR-0043).
    const float3 emission = material.emissiveFactor *
                            SampleTexture(material.emissiveTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).rgb * frame.exposure;

    const float3 normal = SurfaceNormal(input, isFrontFace, material, frame.normalStrength);

    const float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    const float3 toCamera = frame.cameraPosition - input.worldPosition;
    const float cameraDistance = length(toCamera);
    const float3 view = toCamera / max(cameraDistance, 0.0001);

    const float3 specularAlbedo = SpecularAlbedo(f0, roughness, max(dot(normal, view), 0.0));
    ShadingOutput output;
    output.base = float4(emission, 1.0);
    float3 diffuseLight = float3(0.0, 0.0, 0.0);
    float3 specularLight = float3(0.0, 0.0, 0.0);
    bool stochastic = false;
    // Indirect light, which the material's occlusion darkens: direct light has its shadows.
    const int3 texel = int3(int2(input.clipPosition.xy), 0);
    if (frame.indirectTexture != kNoTexture) {
        const float3 indirect = g_textures[frame.indirectTexture].Load(texel).rgb * occlusion;
        diffuseLight += indirect;
        specularLight += (frame.reflectionTexture != kNoTexture
                              ? g_textures[frame.reflectionTexture].Load(texel).rgb * occlusion
                              : indirect) *
                         specularAlbedo;
        stochastic = true;
    }
    // The face's own normal, towards the camera, from which shadow rays start.
    float3 faceNormal = normalize(cross(ddy(input.worldPosition), ddx(input.worldPosition)));
    if (dot(faceNormal, view) < 0.0) {
        faceNormal = -faceNormal;
    }
    const uint2 cluster = g_clusters[frame.clusterBuffer][ClusterOf(frame, input.clipPosition.xy, input.worldPosition)];
    for (uint entry = 0; entry < cluster.y; ++entry) {
        const LightData light = LightAt(
            g_lights[frame.lightBuffer][g_clusterLights[frame.clusterLightBuffer][cluster.x + entry]], input.worldPosition);
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
        // The illuminance the light makes, times the BSDF and the cosine (ADR-0043).
        const BsdfTerms terms = EvaluateBsdfTerms(normal, view, toLight, f0, roughness);
        const float3 diffuse = light.color * attenuation * terms.diffuse;
        const float3 specular = light.color * attenuation * terms.specular;
        // Only light that would add something is worth a ray.
        if (all(diffuse * diffuseColor + specular <= 0.0)) {
            continue;
        }
        const float2 u = LightSample(input.clipPosition.xy, frame.frameIndex, entry);
        const float3 toPoint = SampleLightPoint(light.position, light.radius, input.worldPosition, u) - input.worldPosition;
        const float distanceToPoint = length(toPoint);
        stochastic = stochastic || light.radius > 0.0;
        if (LightReaches(input.worldPosition, faceNormal, toPoint / max(distanceToPoint, 0.0001), distanceToPoint,
                         light.range, frame.accelerationStructure, g_push.instanceBuffer, g_push.materialBuffer)) {
            diffuseLight += diffuse;
            specularLight += specular;
        }
    }

    // Divided by the albedo's luminance, which keeps the color of a metal's reflections in the light.
    const float albedo = max(dot(specularAlbedo, float3(0.2126, 0.7152, 0.0722)), 0.001);
    output.diffuse = float4(diffuseLight, stochastic ? 1.0 : 0.0);
    output.specular = float4(specularLight / albedo, albedo);
    output.albedo = float4(diffuseColor, 1.0);
    return output;
}
