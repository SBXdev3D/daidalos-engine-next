// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Writes the material grid (ADR-0094, MaterialGrid.slangh): one thread per cell tests the boxes around the
// frame's decals and wet volumes, in world space, against the cell's, and sets the bits of those that overlap.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "MaterialGrid.slangh"

// Matches MaterialGridPushConstants in renderer/src/MeshPass.cpp.
struct MaterialGridPushConstants {
    uint frameBuffer;
    uint boxes; // The decals' boxes, then the wet volumes', each its lowest then its highest corner.
    uint grid;
};

[[vk::push_constant]] MaterialGridPushConstants g_grid;
[[vk::binding(2, 0)]] StructuredBuffer<float4> g_boxes[];
[[vk::binding(2, 0)]] RWStructuredBuffer<uint4> g_cells[];

// Matches kMaxDecals in renderer/src/MeshPass.cpp: the wet volumes' boxes come after this many.
static const uint kMaxDecals = 64;

bool Overlaps(uint box, float3 lowest, float3 highest) {
    const float3 boxLowest = g_boxes[g_grid.boxes][box * 2].xyz;
    const float3 boxHighest = g_boxes[g_grid.boxes][box * 2 + 1].xyz;
    return all(boxLowest <= highest) && all(boxHighest >= lowest);
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id: SV_DispatchThreadID) {
    if (any(id >= kMaterialGridCells)) {
        return;
    }
    const FrameData frame = g_frames[g_grid.frameBuffer][0];
    const float3 lowest = frame.materialGridOrigin + float3(id) * frame.materialGridCell;
    const float3 highest = lowest + frame.materialGridCell;
    uint4 bits = uint4(0, 0, 0, 0);
    for (uint decal = 0; decal < frame.decalCount; ++decal) {
        if (Overlaps(decal, lowest, highest)) {
            bits[decal / 32] |= 1u << (decal % 32);
        }
    }
    for (uint volume = 0; volume < frame.wetVolumeCount; ++volume) {
        if (Overlaps(kMaxDecals + volume, lowest, highest)) {
            bits.z |= 1u << volume;
        }
    }
    g_cells[g_grid.grid][(id.z * kMaterialGridCells.y + id.y) * kMaterialGridCells.x + id.x] = bits;
}
