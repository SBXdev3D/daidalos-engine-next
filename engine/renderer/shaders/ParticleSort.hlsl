// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// One step of a bitonic sort of the particles by depth, farthest first, so that blending draws them back
// to front (ADR-0025). The CPU dispatches every (block, step) pair over the whole pool.

#include "Particles.hlsli"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint i = id.x;
    const uint partner = i ^ g_particles.sortStep;
    if (i >= g_particles.capacity || partner <= i) {
        return;
    }
    const uint2 a = g_particleKeys[g_particles.keys][i];
    const uint2 b = g_particleKeys[g_particles.keys][partner];
    // Blocks alternate their order, so that each pair of blocks forms a bitonic sequence.
    const bool farthestFirst = (i & g_particles.sortBlock) == 0;
    if (farthestFirst ? (a.x < b.x) : (a.x > b.x)) {
        g_particleKeys[g_particles.keys][i] = b;
        g_particleKeys[g_particles.keys][partner] = a;
    }
}
