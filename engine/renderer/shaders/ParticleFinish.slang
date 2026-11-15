// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// After the simulation (ADR-0025): the slots of the sort past the alive particles get a depth of 0, which
// sorts them last; thread 0 writes the arguments of the draw, makes the next alive list current, and empties
// the list of sub-spawns this frame spawned (ADR-0053).

#include "Particles.slangh"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint alive = g_particleLists[g_particles.counters][kNextAliveCounter];
    if (id.x < g_particles.capacity && id.x >= alive) {
        g_particleKeys[g_particles.keys][id.x] = uint2(0, 0);
    }
    if (id.x == 0) {
        // Six vertices make a particle's quad; one instance per particle.
        g_particleLists[g_particles.arguments][0] = 6;
        g_particleLists[g_particles.arguments][1] = alive;
        g_particleLists[g_particles.arguments][2] = 0;
        g_particleLists[g_particles.arguments][3] = 0;
        g_particleLists[g_particles.counters][kAliveCounter] = alive;
        g_particleLists[g_particles.counters][kNextAliveCounter] = 0;
        // This frame spawned the last frame's sub-spawns: their list is free for the next frame's simulation.
        const ParticleFrame frame = g_particleFrames[g_particles.parameters][0];
        g_particleLists[g_particles.counters][frame.subCounterIn] = 0;
        // The CPU reads how many are alive with the frame's hits, for the statistics.
        g_particleLists[frame.hits][1] = alive;
    }
}
