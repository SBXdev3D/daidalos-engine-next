// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Moves every alive particle by one frame (ADR-0025): gravity, drag, and bounces off what the G-buffer
// drew. Particles at the end of their life go back to the dead list; the others go to the next alive
// list, with their depth as the key of the sort.
// It also lights lit particles (ADR-0044): a few lights sampled at the particle's center, with their shadow
// rays, blended with what the particle held, so that its light is smooth across its quad and from frame to
// frame, as a pixel's sampled light, which smoke does not denoise, would not be.

#include "Particles.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

// Meters behind a surface where a particle still counts as touching it: farther, it is behind something.
static const float kCollisionThickness = 0.5;
// Lights sampled per particle and frame, and how much of its light a frame's samples replace.
static const uint kLightSamples = 4;
static const float kLightBlend = 0.15;

// The light of the frame's lights at a point of smoke: from every side, as a white matte surface facing each
// light, its illuminance over pi (ADR-0043), with shadows.
float3 SmokeLight(FrameData frame, float3 position, uint seed) {
    Receiver receiver;
    receiver.position = position;
    receiver.origin = position;
    receiver.faceNormal = -frame.cameraForward;
    receiver.normal = receiver.faceNormal;
    receiver.view = receiver.faceNormal;
    receiver.diffuseColor = float3(1.0, 1.0, 1.0);
    receiver.f0 = float3(0.0, 0.0, 0.0);
    receiver.roughness = 1.0;
    receiver.response = kRespondEverywhere;
    receiver.emissiveSpecular = true;
    uint random = Pcg(seed);
    float3 light = float3(0.0, 0.0, 0.0);
    for (uint sample = 0; sample < kLightSamples; ++sample) {
        light += SampleDirectLight(frame, receiver, random, g_particles.instanceBuffer, g_particles.materialBuffer);
    }
    return light / float(kLightSamples);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_particleLists[g_particles.counters][kAliveCounter]) {
        return;
    }
    const uint index = g_particleLists[g_particles.aliveIn][id.x];
    Particle particle = g_particleData[g_particles.particles][index];
    particle.age += g_particles.timeStep;
    if (particle.age >= particle.lifetime) {
        uint dead;
        InterlockedAdd(g_particleLists[g_particles.counters][kDeadCounter], 1, dead);
        g_particleLists[g_particles.dead][dead] = index;
        return;
    }

    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][particle.effect];
    const float step = g_particles.timeStep;
    particle.velocity.y -= 9.81 * effect.gravity * step;
    particle.velocity *= max(1.0 - effect.drag * step, 0.0);
    particle.position += particle.velocity * step;
    particle.rotation += particle.rotationSpeed * step;

    const float depth = dot(particle.position - frame.cameraPosition, frame.cameraForward);
    if ((effect.flags & kEffectCollides) != 0 && depth > frame.nearPlane) {
        // On screen, and just behind the surface drawn there: bounce off it.
        const float4 clip = mul(frame.viewProjection, float4(particle.position, 1.0));
        const float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (all(uv >= 0.0) && all(uv <= 1.0)) {
            const float sceneDepth = LinearDepth(
                frame, g_textures[g_particles.depthTexture].SampleLevel(g_samplers[kSamplerNearestClamp], uv, 0.0).r);
            if (depth > sceneDepth && depth < sceneDepth + kCollisionThickness) {
                const float3 normal = normalize(
                    g_textures[g_particles.normalTexture].SampleLevel(g_samplers[kSamplerNearestClamp], uv, 0.0).xyz *
                        2.0 - 1.0);
                if (dot(particle.velocity, normal) < 0.0) {
                    particle.velocity = reflect(particle.velocity, normal) * effect.bounce;
                }
                particle.position += normal * (depth - sceneDepth);
            }
        }
    }
    if ((effect.flags & kEffectLit) != 0) {
        const float3 sampled = SmokeLight(frame, particle.position, index * 9781u + g_particles.seed * 26699u);
        particle.light = particle.lightSamples > 0.0 ? lerp(particle.light, sampled, kLightBlend) : sampled;
        particle.lightSamples = min(particle.lightSamples + 1.0, 1.0 / kLightBlend);
    }
    g_particleData[g_particles.particles][index] = particle;

    uint slot;
    InterlockedAdd(g_particleLists[g_particles.counters][kNextAliveCounter], 1, slot);
    g_particleLists[g_particles.aliveOut][slot] = index;
    // Depths are positive, so their bits sort in the same order as the numbers.
    g_particleKeys[g_particles.keys][slot] = uint2(asuint(max(depth, 1.0e-6)), index);
}
