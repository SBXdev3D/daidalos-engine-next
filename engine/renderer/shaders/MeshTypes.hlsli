// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#ifndef DAIDALOS_MESH_TYPES_HLSLI
#define DAIDALOS_MESH_TYPES_HLSLI

// Matches MeshPushConstants in engine/renderer/src/MeshPass.cpp.
struct MeshPushConstants {
    float4x4 viewProjection;
    float4x4 world;
};

#endif
