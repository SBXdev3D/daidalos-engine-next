// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Moves every alive particle by one frame (ADR-0025, ADR-0053): gravity, the drag of the air, which the wind
// moves, turbulence and attractors, then along its motion against the frame's acceleration structure, where
// it bounces, sticks or dies. Particles at the end of their life go back to the dead list; the others go to
// the next alive list, with their depth as the key of the sort. Where a particle dies or hits a surface, its
// effect can ask the next frame for particles of another effect, and report the hit to the CPU.
// It also lights lit particles (ADR-0044). Each keeps a reservoir, as a pixel does in ReSTIR DI: the light it
// chose, combined each frame with a few new candidates, so that a puff learns which light matters to it,
// such as a small lamp beside it that power alone rarely draws. One shadow ray tests the light, and what it
// sends is blended with what the particle held, smooth across its quad and from frame to frame: smoke has
// no denoiser.

#include "Particles.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

// How far off a surface a particle that touched it is put, so that its next ray starts in front of it.
static const float kSurfaceOffset = 0.005;
// Below this speed off a surface, a bouncing particle comes to rest on it, and no longer reports hits.
static const float kRestSpeed = 0.3;
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

// How much of the light a puff scatters towards the camera, relative to a puff that scatters it the same
// way everywhere (ADR-0053): the Henyey-Greenstein phase function, times 4 pi. light travels from the light,
// view goes to the camera; anisotropy above 0 sends it on forwards, so smoke glows lit from behind.
float PhaseScale(float anisotropy, float3 light, float3 view) {
    const float g = clamp(anisotropy, -0.95, 0.95);
    const float denominator = 1.0 + g * g - 2.0 * g * dot(light, view);
    return (1.0 - g * g) / (denominator * sqrt(denominator));
}

// The particle's reservoir after this frame, and the light it sends towards view, scattered with the
// anisotropy; direction is set towards the light it kept, if any.
float3 LightSmoke(FrameData frame, Receiver receiver, inout uint4 stored, bool first, uint seed, float3 view,
                  float anisotropy, inout float3 direction) {
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
            light = contact.illuminance * Reflected(receiver, LightTerms(receiver, contact)) * combined.weight *
                    PhaseScale(anisotropy, -contact.toLight, view);
            direction = contact.toLight;
        } else {
            // Hidden: the next frame does not keep it.
            combined.light = kNoLight;
            combined.weight = 0.0;
        }
    }
    stored = Pack(combined);
    return light;
}

// Gradient noise (Perlin 2002) of a point, about -1 to 1: a random gradient at each corner of its cell.
float GradientNoise(float3 p) {
    const float3 cell = floor(p);
    const float3 f = p - cell;
    const float3 fade = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float corners[8];
    for (uint corner = 0; corner < 8; ++corner) {
        const float3 offset = float3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        const int3 at = int3(cell + offset);
        uint seed = Hash(uint(at.x) * 73856093u ^ uint(at.y) * 19349663u ^ uint(at.z) * 83492791u);
        const float3 gradient = normalize(float3(Random(seed), Random(seed), Random(seed)) * 2.0 - 1.0 + 0.0001);
        corners[corner] = dot(gradient, f - offset);
    }
    const float4 x = lerp(float4(corners[0], corners[2], corners[4], corners[6]),
                          float4(corners[1], corners[3], corners[5], corners[7]), fade.x);
    const float2 y = lerp(x.xz, x.yw, fade.y);
    return lerp(y.x, y.y, fade.z);
}

// Three noises, offset from each other: a field whose curl swirls without sources or sinks.
float3 Potential(float3 p) {
    return float3(GradientNoise(p), GradientNoise(p + float3(31.4, 17.3, 5.9)), GradientNoise(p + float3(-12.7, 43.1, 27.2)));
}

// The curl of the potential, by central differences: a velocity field that turns smoke into eddies
// (Bridson et al. 2007), divergence free so that particles neither bunch up nor thin out.
float3 Curl(float3 p) {
    const float e = 0.05;
    const float3 dx = Potential(p + float3(e, 0.0, 0.0)) - Potential(p - float3(e, 0.0, 0.0));
    const float3 dy = Potential(p + float3(0.0, e, 0.0)) - Potential(p - float3(0.0, e, 0.0));
    const float3 dz = Potential(p + float3(0.0, 0.0, e)) - Potential(p - float3(0.0, 0.0, e));
    return float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) / (2.0 * e);
}

// How strong a wind is now, around 1: gusts of a frequency, out of step from place to place along it.
float Gust(float3 position, float3 velocity, float gust, float frequency, float time) {
    if (gust <= 0.0) {
        return 1.0;
    }
    const float phase = 6.2831853 * frequency * time - dot(position, velocity) * 0.05;
    return max(1.0 + gust * (0.6 * sin(phase) + 0.4 * sin(phase * 2.37 + 1.3)), 0.0);
}

// The air's velocity at a point: the level's wind and the boxes of wind it is in.
float3 AirVelocity(ParticleFrame frame, float3 position, float time) {
    float3 air = frame.wind * Gust(position, frame.wind, frame.windGust, frame.windGustFrequency, time);
    for (uint i = 0; i < frame.forceCount; ++i) {
        const ParticleForce force = g_particleForces[frame.forces][i];
        if (force.kind != kForceWind) {
            continue;
        }
        const float4 point = float4(position, 1.0);
        const float3 local = float3(dot(force.toBox0, point), dot(force.toBox1, point), dot(force.toBox2, point));
        // 1 at the heart of the box, 0 at its faces; it fades over the edge's fraction of that.
        const float3 inside = 1.0 - abs(local) * 2.0;
        const float depth = min(inside.x, min(inside.y, inside.z));
        if (depth <= 0.0) {
            continue;
        }
        const float weight = force.edge > 0.0 ? saturate(depth / force.edge) : 1.0;
        air += force.vector * weight * Gust(position, force.vector, force.gust, force.gustFrequency, time);
    }
    return air;
}

// The pull and the turn of the attractors that move this effect.
float3 Attraction(ParticleFrame frame, float3 position, uint effect) {
    float3 acceleration = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < frame.forceCount; ++i) {
        const ParticleForce force = g_particleForces[frame.forces][i];
        if (force.kind != kForceAttractor || (force.effect != kNoEffect && force.effect != effect)) {
            continue;
        }
        const float3 away = position - force.vector;
        const float distance = length(away);
        if (distance < 1.0e-3) {
            continue;
        }
        const float falloff = force.radius > 0.0 ? saturate(1.0 - distance / force.radius) : 1.0;
        const float3 around = cross(force.axis, away);
        const float aroundLength = length(around);
        acceleration -= away / distance * force.strength * falloff;
        if (aroundLength > 1.0e-4) {
            acceleration += around / aroundLength * force.vortex * falloff;
        }
    }
    return acceleration;
}

// Asks the next frame for count particles of an effect, at a place.
void SubSpawn(ParticleFrame frame, uint effect, uint count, float3 position, float3 direction, float3 velocity) {
    if (effect == kNoEffect || count == 0) {
        return;
    }
    uint slot;
    InterlockedAdd(g_particleLists[g_particles.counters][frame.subCounterOut], 1, slot);
    if (slot >= kMaxSubSpawns) {
        return;
    }
    ParticleSubSpawn spawn;
    spawn.position = position;
    spawn.effect = effect;
    spawn.direction = direction;
    spawn.count = count;
    spawn.velocity = velocity;
    spawn.padding = 0;
    g_particleSubSpawns[frame.subSpawnsOut][slot] = spawn;
}

// Tells the CPU of a hit, if this frame's buffer has room.
void ReportHit(ParticleFrame frame, float3 position, float3 normal, uint effect, uint material) {
    uint slot;
    InterlockedAdd(g_particleLists[frame.hits][0], 1, slot);
    if (slot >= frame.hitCapacity) {
        return;
    }
    ParticleHitData hit;
    hit.position = position;
    hit.effect = effect;
    hit.normal = normal;
    hit.material = material;
    g_particleHits[frame.hits][slot + 1] = hit;
}

// Puts a particle back in the dead list.
void Kill(uint index) {
    uint dead;
    InterlockedAdd(g_particleLists[g_particles.counters][kDeadCounter], 1, dead);
    g_particleLists[g_particles.dead][dead] = index;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_particleLists[g_particles.counters][kAliveCounter]) {
        return;
    }
    const uint index = g_particleLists[g_particles.aliveIn][id.x];
    Particle particle = g_particleData[g_particles.particles][index];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][particle.effect];
    const ParticleFrame air = g_particleFrames[g_particles.parameters][0];
    particle.age += g_particles.timeStep;
    if (particle.age >= particle.lifetime) {
        Kill(index);
        SubSpawn(air, effect.deathEffect, effect.deathCount, particle.position, particle.axis, particle.velocity);
        return;
    }

    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const float step = g_particles.timeStep;
    // Size, emission and speed over life (ADR-0053).
    const float4 scales = SampleCurve(particle.effect, kCurveScales, particle.age / particle.lifetime);
    if ((particle.state & kParticleResting) == 0) {
        // Gravity, the drag of the moving air, turbulence and attractors.
        particle.velocity.y -= 9.81 * effect.gravity * step;
        if (effect.drag > 0.0) {
            const float3 wind = AirVelocity(air, particle.position, frame.time);
            particle.velocity = lerp(particle.velocity, wind, min(effect.drag * step, 1.0));
        }
        if (effect.turbulence > 0.0) {
            const float3 drift = float3(0.0, 0.3, 0.2) * frame.time;
            particle.velocity += Curl(particle.position / max(effect.turbulenceScale, 0.01) + drift) * effect.turbulence * step;
        }
        particle.velocity += Attraction(air, particle.position, particle.effect) * step;

        const float3 motion = particle.velocity * scales.z * step;
        const float distance = length(motion);
        Hit hit;
        hit.found = false;
        if (effect.collision != kCollisionNone && distance > 1.0e-6) {
            // Along its motion, against everything the frame's rays see, glass included.
            RayDesc ray;
            ray.Origin = particle.position;
            ray.Direction = motion / distance;
            ray.TMin = 0.0;
            ray.TMax = distance;
            hit = TraceClosest(ray, frame.accelerationStructure, g_particles.instanceBuffer, g_particles.materialBuffer,
                               kMaskAll);
        }
        if (hit.found) {
            const InstanceData instance = g_instances[g_particles.instanceBuffer][hit.instance];
            float3 normal = normalize(FaceNormal(instance, hit.primitive));
            if (dot(normal, motion) > 0.0) {
                normal = -normal; // The back of a double-sided surface.
            }
            const float3 contact = particle.position + motion / distance * hit.distance + normal * kSurfaceOffset;
            // An impact, not a particle sliding along the surface that gravity presses against it each frame:
            // only impacts are reported and spawn particles.
            if (-dot(particle.velocity, normal) > kRestSpeed || effect.collision == kCollisionDie) {
                if ((effect.flags & kEffectReportsHits) != 0) {
                    ReportHit(air, contact, normal, particle.effect, instance.material);
                }
                SubSpawn(air, effect.hitEffect, effect.hitCount, contact, normal, particle.velocity);
            }
            if (effect.collision == kCollisionDie) {
                Kill(index);
                return;
            }
            particle.position = contact;
            if (effect.collision == kCollisionStick) {
                particle.velocity = float3(0.0, 0.0, 0.0);
                particle.state |= kParticleResting;
            } else {
                // It bounces off, or slides along when the bounce is too weak to leave, and rests once slow.
                particle.velocity = reflect(particle.velocity, normal) * effect.bounce;
                const float off = dot(particle.velocity, normal);
                if (off < kRestSpeed) {
                    particle.velocity -= normal * off;
                    if (length(particle.velocity) < kRestSpeed) {
                        particle.velocity = float3(0.0, 0.0, 0.0);
                        particle.state |= kParticleResting;
                    }
                }
            }
        } else {
            particle.position += motion;
        }
    }
    particle.rotation += particle.rotationSpeed * step;
    const float depth = dot(particle.position - frame.cameraPosition, frame.cameraForward);

    if ((effect.flags & kEffectLit) != 0) {
        const bool first = particle.lightSamples <= 0.0;
        const float radius = particle.size * scales.x * 0.5;
        const float3 view = normalize(frame.cameraPosition - particle.position + 0.0001);
        const float3 sampled = LightSmoke(frame, SmokeReceiver(frame, particle.position, radius), particle.reservoir,
                                          first, index * 9781u + g_particles.seed * 26699u, view, effect.anisotropy,
                                          particle.lightDirection);
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
