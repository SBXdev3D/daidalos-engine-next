// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Bitonic sort of the particles by depth, farthest first, so that blending draws them back to front
// (ADR-0025). Steps that compare keys at least kLocalSize apart run one per dispatch, over the whole pool;
// the shorter steps of a block run together in group-shared memory, kLocalSize keys per group. With
// sortStep 0, a group sorts every block from 2 up to sortBlock if that fits in it, or else finishes the
// block sortBlock, whose longer steps are done.

#include "Particles.slangh"

#define kThreads 1024
#define kLocalSize (2 * kThreads)

groupshared uint2 s_keys[kLocalSize];

// Puts the pair in the order its block wants: blocks alternate, so that two of them form a bitonic sequence.
bool Ordered(uint2 a, uint2 b, uint index, uint block) {
    const bool farthestFirst = (index & block) == 0;
    return farthestFirst ? (a.x >= b.x) : (a.x <= b.x);
}

[numthreads(kThreads, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    RWStructuredBuffer<uint2> keys = g_particleKeys[g_particles.keys];
    if (g_particles.sortStep != 0) {
        // A long step: thread i compares the key i of the lower half of each pair of runs to its partner.
        const uint step = g_particles.sortStep;
        const uint i = 2 * step * (id.x / step) + id.x % step;
        const uint partner = i + step;
        if (partner >= g_particles.capacity) {
            return;
        }
        const uint2 a = keys[i];
        const uint2 b = keys[partner];
        if (!Ordered(a, b, i, g_particles.sortBlock)) {
            keys[i] = b;
            keys[partner] = a;
        }
        return;
    }

    const uint base = group.x * kLocalSize;
    s_keys[thread] = keys[base + thread];
    s_keys[thread + kThreads] = keys[base + thread + kThreads];
    GroupMemoryBarrierWithGroupSync();
    const uint lastBlock = g_particles.sortBlock;
    for (uint block = lastBlock <= kLocalSize ? 2 : lastBlock; block <= lastBlock; block *= 2) {
        for (uint step = min(block, kLocalSize) / 2; step > 0; step /= 2) {
            const uint i = 2 * step * (thread / step) + thread % step;
            const uint2 a = s_keys[i];
            const uint2 b = s_keys[i + step];
            if (!Ordered(a, b, base + i, block)) {
                s_keys[i] = b;
                s_keys[i + step] = a;
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
    keys[base + thread] = s_keys[thread];
    keys[base + thread + kThreads] = s_keys[thread + kThreads];
}
