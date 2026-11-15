// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// After the simulation (ADR-0025, ADR-0053): the slots of the sort past the alive particles take the frame's
// ribbon segments, by the depth of their middle, and the rest a depth of 0, which sorts them last. Thread 0
// writes the arguments of the quads' draw, which leaves out mesh particles and takes the ribbons in, makes
// the next alive list current, and empties the list of sub-spawns this frame spawned; the first threads
// write each mesh effect's draw.

#include "Particles.slangh"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint alive = g_particleLists[g_particles.counters][kNextAliveCounter];
    const ParticleFrame frame = g_particleFrames[g_particles.parameters][0];
    // Ribbons take the slots the pool leaves free.
    const uint segments = min(frame.ribbonSegmentCount, g_particles.capacity - min(alive, g_particles.capacity));
    if (id.x < g_particles.capacity && id.x >= alive) {
        const uint segment = id.x - alive;
        uint2 key = uint2(0, 0);
        if (segment < segments) {
            const FrameData view = g_frames[g_particles.frameBuffer][0];
            const uint first = g_ribbonSegments[frame.ribbonSegments][segment];
            const float3 middle = 0.5 * (g_ribbonPoints[frame.ribbonPoints][first].position +
                                         g_ribbonPoints[frame.ribbonPoints][first + 1].position);
            const float depth = dot(middle - view.cameraPosition, view.cameraForward);
            key = uint2(asuint(max(depth, 1.0e-6)), segment | kRibbonKey);
        }
        g_particleKeys[g_particles.keys][id.x] = key;
    }
    if (id.x < kMaxMeshEffects) {
        // Each mesh effect's indexed draw: as many instances as its list holds.
        const uint base = kMeshArguments + id.x * 5;
        const uint count = min(g_particleLists[g_particles.counters][kMeshCounters + id.x], kMeshCapacity);
        g_particleLists[g_particles.arguments][base] = frame.meshIndexCounts[id.x / 4][id.x % 4];
        g_particleLists[g_particles.arguments][base + 1] = count;
        g_particleLists[g_particles.arguments][base + 2] = 0;
        g_particleLists[g_particles.arguments][base + 3] = 0;
        g_particleLists[g_particles.arguments][base + 4] = 0;
        g_particleLists[g_particles.counters][kMeshCounters + id.x] = 0;
    }
    if (id.x == 0) {
        // Six vertices make a particle's quad, or a ribbon's segment; one instance each.
        const uint meshes = g_particleLists[g_particles.counters][kMeshAliveCounter];
        g_particleLists[g_particles.arguments][0] = 6;
        g_particleLists[g_particles.arguments][1] = alive - meshes + segments;
        g_particleLists[g_particles.arguments][2] = 0;
        g_particleLists[g_particles.arguments][3] = 0;
        g_particleLists[g_particles.counters][kAliveCounter] = alive;
        g_particleLists[g_particles.counters][kNextAliveCounter] = 0;
        g_particleLists[g_particles.counters][kMeshAliveCounter] = 0;
        // This frame spawned the last frame's sub-spawns: their list is free for the next frame's simulation.
        g_particleLists[g_particles.counters][frame.subCounterIn] = 0;
        // The CPU reads how many are alive with the frame's hits, for the statistics.
        g_particleLists[frame.hits][1] = alive;
    }
}
