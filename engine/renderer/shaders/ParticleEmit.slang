// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Spawns the frame's new particles (ADR-0025, ADR-0053): one thread each, taking a free slot from the dead list and
// adding it to the alive list. When the pool is full, new particles are dropped.

#include "Particles.slangh"

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_particles.emitCount) {
        return;
    }
    // The spawn request this particle belongs to.
    uint request = 0;
    while (request + 1 < g_particles.spawnCount &&
           id.x >= g_particleSpawns[g_particles.spawns][request + 1].first) {
        ++request;
    }
    const ParticleSpawn spawn = g_particleSpawns[g_particles.spawns][request];

    uint dead;
    InterlockedAdd(g_particleLists[g_particles.counters][kDeadCounter], 0xFFFFFFFF, dead);
    if (dead == 0 || dead > g_particles.capacity) {
        InterlockedAdd(g_particleLists[g_particles.counters][kDeadCounter], 1); // The pool is full.
        return;
    }
    const uint index = g_particleLists[g_particles.dead][dead - 1];

    const ParticleEffectData effect = g_particleEffects[g_particles.effects][spawn.effect];
    uint seed = Hash(id.x ^ (g_particles.seed * 9781u));
    const float3 axis = normalize(effect.direction + 0.0001);
    const float3 helper = abs(axis.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 sideA = normalize(cross(helper, axis));
    const float3 sideB = cross(axis, sideA);

    // Where it appears, in the emitter's frame (renderer::ParticleShape).
    float3 offset = float3(0.0, 0.0, 0.0);
    float3 direction;
    if (effect.shape == kShapeCone) {
        // On a disc across the direction, leaving it opened out by up to the spread at the rim.
        const float radius = sqrt(Random(seed));
        const float turn = Random(seed) * 6.2831853;
        const float3 outward = sideA * cos(turn) + sideB * sin(turn);
        offset = outward * radius * effect.spawnRadius;
        const float angle = radius * effect.spread;
        direction = axis * cos(angle) + outward * sin(angle);
    } else {
        if (effect.shape == kShapeCylinder) {
            const float radius = sqrt(Random(seed)) * effect.spawnRadius;
            const float turn = Random(seed) * 6.2831853;
            offset = (sideA * cos(turn) + sideB * sin(turn)) * radius + axis * (Random(seed) * 2.0 - 1.0) * effect.spawnBox.y;
        } else if (effect.shape == kShapeLine) {
            offset = float3((Random(seed) * 2.0 - 1.0) * effect.spawnBox.x, 0.0, 0.0);
        } else {
            // A point in the box, moved by one in the sphere.
            offset = (float3(Random(seed), Random(seed), Random(seed)) * 2.0 - 1.0) * effect.spawnBox;
            const float3 inSphere = normalize(float3(Random(seed), Random(seed), Random(seed)) * 2.0 - 1.0 + 0.0001);
            offset += inSphere * effect.spawnRadius * pow(Random(seed), 1.0 / 3.0);
        }
        // A direction within the spread of the effect's.
        const float cosine = lerp(1.0, cos(effect.spread), Random(seed));
        const float sine = sqrt(saturate(1.0 - cosine * cosine));
        const float turn = Random(seed) * 6.2831853;
        direction = axis * cosine + (sideA * cos(turn) + sideB * sin(turn)) * sine;
    }

    // Spread along the way the emitter went this frame, each particle in its own stretch of it.
    const float along = (float(id.x - spawn.first) + Random(seed)) / float(max(spawn.count, 1));
    Particle particle;
    particle.position = lerp(spawn.previous, spawn.position, along) + Rotate(spawn.rotation, offset);
    particle.velocity = Rotate(spawn.rotation, direction) * lerp(effect.speedMin, effect.speedMax, Random(seed)) +
                        spawn.velocity * effect.inheritVelocity;
    particle.age = 0.0;
    particle.lifetime = max(lerp(effect.lifeMin, effect.lifeMax, Random(seed)), 0.001);
    particle.size = lerp(effect.sizeMin, effect.sizeMax, Random(seed));
    particle.rotation = Random(seed) * 6.2831853;
    particle.rotationSpeed = (Random(seed) * 2.0 - 1.0) * effect.rotationSpeed;
    particle.effect = spawn.effect;
    particle.light = float3(0.0, 0.0, 0.0);
    particle.lightSamples = 0.0;
    particle.reservoir = uint4(0xFFFFFFFF, 0, 0, 0);
    particle.axis = normalize(Rotate(spawn.rotation, effect.axis) + 0.0001);
    particle.startFrame = (effect.flags & kEffectRandomStart) != 0 ? floor(Random(seed) * float(effect.frames)) : 0.0;
    g_particleData[g_particles.particles][index] = particle;

    uint slot;
    InterlockedAdd(g_particleLists[g_particles.counters][kAliveCounter], 1, slot);
    g_particleLists[g_particles.aliveIn][slot] = index;
}
