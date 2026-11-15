// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Moves every alive particle by one frame (ADR-0025): gravity, drag, and bounces off what the G-buffer
// drew. Particles at the end of their life go back to the dead list; the others go to the next alive
// list, with their depth as the key of the sort.
// It also lights lit particles (ADR-0044). Each keeps a reservoir, as a pixel does in ReSTIR DI: the light it
// chose, combined each frame with a few new candidates, so that a puff learns which light matters to it,
// such as a small lamp beside it that power alone rarely draws. One shadow ray tests the light, and what it
// sends is blended with what the particle held, smooth across its quad and from frame to frame: smoke has
// no denoiser.

#include "Particles.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

// Meters behind a surface where a particle still counts as touching it: farther, it is behind something.
static const float kCollisionThickness = 0.5;
// Reservoirs of new candidates a particle draws on its first frame, and on the others; how much of its light
// a frame replaces.
static const uint kFirstDraws = 4;
static const uint kDraws = 1;
static const float kLightBlend = 0.15;

// A puff of smoke of this radius as a receiver of light, from its center: from every side, as a white matte
// surface facing each light, its illuminance over pi (ADR-0043). No light counts as nearer than the
// radius: a puff is lit as a whole.
Receiver SmokeReceiver(FrameData frame, float3 position, float radius) {
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
    receiver.nearest = max(radius, kNearestLight);
    return receiver;
}

// Adds a reservoir to another, both of the same receiver, weighed by their confidence; target is the
// receiver's target value of what it holds.
void Merge(inout Reservoir combined, Reservoir added, float target, float share, float pick) {
    Stream(combined, added.light, added.u, share * target * added.weight, target, pick);
}

// The particle's reservoir after this frame, and the light it sends.
float3 LightSmoke(FrameData frame, Receiver receiver, inout uint4 stored, bool first, uint seed) {
    uint random = Pcg(seed);
    Reservoir history = Unpack(stored);
    history.count = first ? 0.0 : min(history.count, kMaxHistory);
    if (history.light != kNoLight) {
        history.light = history.light < g_particles.previousLightCount ? g_lightMaps[g_particles.lightMap][history.light]
                                                                       : kNoLight;
    }
    // The new candidates, then the history, weighed by confidence; a light the last frame did not have, only
    // the new candidates can have found.
    const uint draws = first ? kFirstDraws : kDraws;
    const float total = float(draws) + history.count;
    Reservoir combined = EmptyReservoir();
    for (uint draw = 0; draw < draws; ++draw) {
        const Reservoir fresh = DrawCandidates(frame, receiver, random);
        const bool isNew = fresh.light != kNoLight &&
                           (g_lightMaps[g_particles.lightMap][g_particles.previousLightCount + fresh.light] & kLightNew) != 0;
        const float target = TargetValue(receiver, ContactLight(frame, fresh.light, fresh.u, receiver));
        Merge(combined, fresh, target, isNew ? 1.0 / float(draws) : 1.0 / total, Random(random));
    }
    if (history.light != kNoLight) {
        const float target = TargetValue(receiver, ContactLight(frame, history.light, history.u, receiver));
        Merge(combined, history, target, history.count / total, Random(random));
    }
    combined.count = total;
    combined.weight = combined.target > 0.0 ? combined.weightSum / combined.target : 0.0;
    if (combined.weight <= 0.0) {
        combined.light = kNoLight;
    }
    float3 light = float3(0.0, 0.0, 0.0);
    if (combined.light != kNoLight) {
        const LightContact contact = ContactLight(frame, combined.light, combined.u, receiver);
        if (Unoccluded(frame, receiver, contact, g_particles.instanceBuffer, g_particles.materialBuffer)) {
            light = contact.illuminance * Reflected(receiver, LightTerms(receiver, contact)) * combined.weight;
        } else {
            // Hidden: the next frame does not keep it.
            combined.light = kNoLight;
            combined.weight = 0.0;
        }
    }
    stored = Pack(combined);
    return light;
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
    // Size, emission and speed over life (ADR-0053).
    const float4 scales = SampleCurve(particle.effect, kCurveScales, particle.age / particle.lifetime);
    particle.position += particle.velocity * scales.z * step;
    particle.rotation += particle.rotationSpeed * step;

    const float depth = dot(particle.position - frame.cameraPosition, frame.cameraForward);
    if ((effect.flags & kEffectCollides) != 0 && depth > frame.nearPlane) {
        // On screen, and just behind the surface drawn there: bounce off it.
        const float4 clip = mul(frame.viewProjection, float4(particle.position, 1.0));
        const float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (all(uv >= 0.0) && all(uv <= 1.0)) {
            const float2 texel = FrameToTarget(frame, uv);
            const float sceneDepth = LinearDepth(
                frame, g_textures[g_particles.depthTexture].SampleLevel(g_samplers[kSamplerNearestClamp], texel, 0.0).r);
            if (depth > sceneDepth && depth < sceneDepth + kCollisionThickness) {
                const float3 normal = normalize(
                    g_textures[g_particles.normalTexture].SampleLevel(g_samplers[kSamplerNearestClamp], texel, 0.0).xyz *
                        2.0 - 1.0);
                if (dot(particle.velocity, normal) < 0.0) {
                    particle.velocity = reflect(particle.velocity, normal) * effect.bounce;
                }
                particle.position += normal * (depth - sceneDepth);
            }
        }
    }
    if ((effect.flags & kEffectLit) != 0) {
        const bool first = particle.lightSamples <= 0.0;
        const float radius = particle.size * scales.x * 0.5;
        const float3 sampled = LightSmoke(frame, SmokeReceiver(frame, particle.position, radius), particle.reservoir,
                                          first, index * 9781u + g_particles.seed * 26699u);
        particle.light = first ? sampled : lerp(particle.light, sampled, kLightBlend);
        particle.lightSamples = min(particle.lightSamples + 1.0, 1.0 / kLightBlend);
    }
    g_particleData[g_particles.particles][index] = particle;

    uint slot;
    InterlockedAdd(g_particleLists[g_particles.counters][kNextAliveCounter], 1, slot);
    g_particleLists[g_particles.aliveOut][slot] = index;
    // Depths are positive, so their bits sort in the same order as the numbers.
    g_particleKeys[g_particles.keys][slot] = uint2(asuint(max(depth, 1.0e-6)), index);
}
