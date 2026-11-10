// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Depth and normal pre-pass (ADR-0024): the depth of the opaque meshes, which the mesh pass then tests
// for equality, so that it shades each pixel once, their world normals, for ambient occlusion, and their
// motion vectors, for temporal antialiasing (ADR-0037). Its pipeline uses the vertex shader of
// Mesh.slang, so that both passes compute the same depth.

#include "MeshCommon.slangh"

struct PrepassOutput {
    float4 normal : SV_Target0;
    // Where the surface was in the previous frame minus where it is, in texture coordinates.
    float2 motion : SV_Target1;
};

PrepassOutput PSMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace) {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const MaterialData material = g_materials[g_push.materialBuffer][g_push.materialIndex];
    BaseColor(material, input.uv); // For the alpha test.
    const float3 normal = SurfaceNormal(input, isFrontFace, material, frame.normalStrength);
    PrepassOutput output;
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    output.motion = ClipToUv(input.previousClip) - ClipToUv(input.currentClip);
    return output;
}
