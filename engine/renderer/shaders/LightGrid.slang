// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Writes the light grid (ADR-0124, LightGrid.slangh): one thread per cell weighs every source of the frame by
// the light it could send to the cell, keeps the kLightGridEntries that send most, and writes the odds of
// drawing each in proportion. The weights only steer sampling: a source left out, or weighed wrong, is
// still drawn from the frame's table, so the light stays unbiased whatever they are.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "LightGrid.slangh"

// Matches LightGridPushConstants in renderer/src/MeshPass.cpp.
struct LightGridPushConstants {
    uint frameBuffer;
    uint sources; // The frame's sources as the grid sees them (GridSource), in the frame's order.
    uint sourceCount;
    uint grid;
};

// One source as the grid weighs it. Matches GridSource in renderer/src/MeshPass.cpp.
struct GridSource {
    float4 sphere;   // Its center, and the radius of the sphere around it.
    float4 axis;     // Where a spot shines, and the cosine of its outer angle: -1 for every direction.
    float4 strength; // Its intensity's luminance in candela (lux for a directional light), its range, and 1
                     // for a directional light, which lights every cell alike.
};

[[vk::push_constant]] LightGridPushConstants g_grid;
[[vk::binding(2, 0)]] StructuredBuffer<GridSource> g_sources[];
[[vk::binding(2, 0)]] RWStructuredBuffer<uint4> g_cells[];

static const float kPi = 3.14159265;

// At most what the source sends to a point of the cell, in lux, from what can be known without rays: its
// intensity over the square of the distance to the cell's center, no nearer than the cell's half diagonal,
// and nothing beyond its range or outside its cone from anywhere in the cell.
float Weigh(GridSource source, float3 center, float halfDiagonal) {
    if (source.strength.z > 0.5) {
        return source.strength.x;
    }
    const float3 offset = center - source.sphere.xyz;
    const float distance = length(offset);
    const float nearest = max(distance - halfDiagonal - source.sphere.w, 0.0);
    const float range = source.strength.y;
    if (nearest >= range) {
        return 0.0;
    }
    if (source.axis.w > -1.0 && distance > halfDiagonal + source.sphere.w) {
        // The cell is outside the cone when the angle to its center, less the angle the cell spans, is wider
        // than the cone's.
        const float angle = acos(clamp(dot(offset / distance, source.axis.xyz), -1.0, 1.0));
        const float spans = asin(saturate((halfDiagonal + source.sphere.w) / distance));
        if (angle - spans > acos(source.axis.w)) {
            return 0.0;
        }
    }
    const float ratio = nearest / range;
    const float ratio2 = ratio * ratio;
    const float window = saturate(1.0 - ratio2 * ratio2);
    const float closest = max(distance, halfDiagonal + source.sphere.w);
    return source.strength.x * window * window / (closest * closest);
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id: SV_DispatchThreadID) {
    if (any(id >= kMaterialGridCells)) {
        return;
    }
    const FrameData frame = g_frames[g_grid.frameBuffer][0];
    const float3 center = frame.materialGridOrigin + (float3(id) + 0.5) * frame.materialGridCell;
    const float halfDiagonal = frame.materialGridCell * 0.8660254;

    // The sources that send most, by weight; the lightest is replaced first.
    float weights[kLightGridEntries];
    uint sources[kLightGridEntries];
    for (uint entry = 0; entry < kLightGridEntries; ++entry) {
        weights[entry] = 0.0;
        sources[entry] = kNoGridSource;
    }
    uint kept = 0;
    uint lightest = 0;
    const uint count = min(g_grid.sourceCount, kNoGridSource);
    for (uint source = 0; source < count; ++source) {
        const float weight = Weigh(g_sources[g_grid.sources][source], center, halfDiagonal);
        if (!(weight > 0.0)) {
            continue;
        }
        if (kept < kLightGridEntries) {
            weights[kept] = weight;
            sources[kept] = source;
            if (kept == 0 || weight < weights[lightest]) {
                lightest = kept;
            }
            ++kept;
            continue;
        }
        if (weight <= weights[lightest]) {
            continue;
        }
        weights[lightest] = weight;
        sources[lightest] = source;
        for (uint entry = 0; entry < kLightGridEntries; ++entry) {
            if (weights[entry] < weights[lightest]) {
                lightest = entry;
            }
        }
    }

    // In the order of the sources, as LightGrid.slangh reads them.
    for (uint entry = 1; entry < kept; ++entry) {
        const float weight = weights[entry];
        const uint source = sources[entry];
        uint place = entry;
        while (place > 0 && sources[place - 1] > source) {
            weights[place] = weights[place - 1];
            sources[place] = sources[place - 1];
            --place;
        }
        weights[place] = weight;
        sources[place] = source;
    }
    float total = 0.0;
    for (uint entry = 0; entry < kept; ++entry) {
        total += weights[entry];
    }

    // The odds of each in 16 bits, cumulated: each at least 1 out of 65535, and the last up to 65535.
    uint words[kLightGridEntries];
    float sum = 0.0;
    uint before = 0;
    for (uint entry = 0; entry < kLightGridEntries; ++entry) {
        if (entry >= kept) {
            words[entry] = kNoGridSource | (0xFFFFu << 16);
            continue;
        }
        sum += weights[entry];
        const uint left = kept - 1 - entry;
        uint upTo = entry + 1 == kept ? 65535u : uint(sum / total * 65535.0 + 0.5);
        upTo = clamp(upTo, before + 1, 65535u - left);
        words[entry] = sources[entry] | (upTo << 16);
        before = upTo;
    }
    const uint first = ((id.z * kMaterialGridCells.y + id.y) * kMaterialGridCells.x + id.x) * 4;
    for (uint word = 0; word < 4; ++word) {
        g_cells[g_grid.grid][first + word] =
            uint4(words[word * 4], words[word * 4 + 1], words[word * 4 + 2], words[word * 4 + 3]);
    }
}
