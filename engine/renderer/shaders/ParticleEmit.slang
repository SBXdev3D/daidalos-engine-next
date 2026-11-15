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
    // The spawn request this particle belongs to.
    uint request = 0;
    while (request + 1 < g_particles.spawnCount && id.x >= g_particleSpawns[g_particles.spawns][request + 1].first) {
        ++request;
    }
    const ParticleSpawn spawn = g_particleSpawns[g_particles.spawns][request];
    uint seed = Hash(id.x ^ (g_particles.seed * 9781u));
    // Each particle in its own stretch of the way the emitter went this frame.
    const float along = (float(id.x - spawn.first) + Random(seed)) / float(max(spawn.count, 1));
    SpawnParticle(spawn.effect, spawn.position, spawn.previous, along, spawn.rotation, spawn.velocity, seed);
}
