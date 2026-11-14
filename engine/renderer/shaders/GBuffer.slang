// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The G-buffer (ADR-0044): the one raster pass over the opaque meshes. It writes their depth, their world
// normals and roughness, their motion vectors (ADR-0037), and what lighting needs of their material: the
// base color and metalness, the emission and the occlusion. Every texture of the material is sampled here,
// once per pixel; the lighting pass and the composite read the result. Glass is left out: the glass pass
// draws it over the finished frame (ADR-0042).
// Its vertex shader, VSMain in MeshCommon.slangh, is the glass pass's too, so that both compute the same
// depth.

#include "MeshCommon.slangh"

struct GBufferOutput {
    // The world normal, times 0.5 plus 0.5, and the roughness.
    float4 normal : SV_Target0;
    // Where the surface was in the previous frame minus where it is, in texture coordinates.
    float2 motion : SV_Target1;
    // The base color, in an sRGB target, and the metalness in alpha, which stays linear.
    float4 baseColor : SV_Target2;
    // The emission in nits, times the frame's exposure (ADR-0043), and the material's occlusion in alpha.
    float4 emission : SV_Target3;
};

GBufferOutput PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];
    const float4 baseColor = BaseColor(material, input.uv); // Discards below the alpha cutoff.
    const float3 normal = SurfaceNormal(input, isFrontFace, material, frame.normalStrength);
    // glTF: roughness in green, metalness in blue.
    const float4 metallicRoughness = SampleTexture(material.metallicRoughnessTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0));
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    const float occlusion =
        lerp(1.0, SampleTexture(material.occlusionTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).r, material.occlusionStrength);
    const float3 emission = material.emissiveFactor *
                            SampleTexture(material.emissiveTexture, input.uv, float4(1.0, 1.0, 1.0, 1.0)).rgb * frame.exposure;

    GBufferOutput output;
    output.normal = float4(normal * 0.5 + 0.5, roughness);
    output.motion = ClipToUv(input.previousClip) - ClipToUv(input.currentClip);
    output.baseColor = float4(baseColor.rgb, metallic);
    output.emission = float4(emission, occlusion);
    return output;
}
