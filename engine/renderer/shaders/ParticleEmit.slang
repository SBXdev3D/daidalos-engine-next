// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Spawns the frame's new particles that the CPU asked for (ADR-0025, ADR-0053): one thread each, taking a
// free slot from the dead list and adding it to the alive list. When the pool is full, new particles are
// dropped.

#include "ParticleSpawning.slangh"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_particles.emitCount) {
        return;
    }
    // The spawn request this particle belongs to: the last whose first particle is not past it. Surfaces
    // ask for one particle per request, so there can be many.
    uint low = 0;
    uint high = g_particles.spawnCount - 1;
    while (low < high) {
        const uint middle = (low + high + 1) / 2;
        if (g_particleSpawns[g_particles.spawns][middle].first <= id.x) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    const uint request = low;
    const ParticleSpawn spawn = g_particleSpawns[g_particles.spawns][request];
    uint seed = Hash(id.x ^ (g_particles.seed * 9781u));
    // Each particle in its own stretch of the way the emitter went this frame.
    const float along = (float(id.x - spawn.first) + Random(seed)) / float(max(spawn.count, 1));
    SpawnParticle(spawn.effect, spawn.position, spawn.previous, along, spawn.rotation, spawn.velocity, spawn.scale, seed);
}
