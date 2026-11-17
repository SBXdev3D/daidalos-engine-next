// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Spawns the particles that the last frame's simulation asked for, where particles died or hit a surface
// (ADR-0053): one thread per request, which spawns its count. The CPU never learns how many there are: the
// dispatch covers every request a frame can make, and threads past the count do nothing. Its first thread
// also empties the frame's list of hits.

#include "ParticleSpawning.slangh"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const ParticleFrame frame = g_particleFrames[g_particles.parameters][0];
    if (id.x == 0) {
        g_particleLists[frame.hits][0] = 0; // The simulation reports this frame's hits next.
        g_particleLists[frame.smoke][0] = 0; // And lists its smoke,
        g_particleLists[frame.hits][2] = 0;  // and the highest slot of a particle rays meet.
    }
    const uint requests = min(g_particleLists[g_particles.counters][frame.subCounterIn], kMaxSubSpawns);
    if (id.x >= requests) {
        return;
    }
    const ParticleSubSpawn spawn = g_particleSubSpawns[frame.subSpawnsIn][id.x];
    const float4 rotation = RotationFromUp(normalize(spawn.direction + 0.0001));
    for (uint i = 0; i < spawn.count; ++i) {
        uint seed = Hash(id.x * 7919u + i * 104729u + g_particles.seed * 26699u);
        if (!SpawnParticle(spawn.effect, spawn.position, spawn.position, 0.0, rotation, spawn.velocity,
                           float3(1.0, 1.0, 1.0), seed)) {
            return;
        }
    }
}
