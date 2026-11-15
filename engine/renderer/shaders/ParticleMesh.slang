// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Mesh particles in the G-buffer (ADR-0053): casings, shards, papers. One indexed draw per mesh effect, an
// instance per particle of its list, which the simulation fills; the pixel shader is the G-buffer's, so that
// they are lit as any surface. A particle is scaled by its size over life; in flight it tumbles about an axis
// of its own, and at rest it lies with its +Y along its axis, the surface's normal, turned about it. Motion
// vectors come from where its velocity and spin put it a step ago.

#define DAI_CUSTOM_VERTEX_SHADER
#define DAI_PARTICLES_NO_PUSH
#define DAI_PARTICLES_READ_ONLY
#include "Particles.slangh"

// The G-buffer's push constants first, which its pixel shader reads, then the particles'.
struct ParticleMeshPushConstants {
    uint frameBuffer;
    uint instanceBuffer; // Unused: no instance.
    uint materialBuffer;
    uint instanceIndex;  // Unused.
    uint materialIndex;
    uint particles;
    uint list;      // The lists of mesh particles.
    uint listStart; // This effect's, in it.
    uint effects;
    uint curves;
    float timeStep;
    uint padding;
};
[[vk::push_constant]] ParticleMeshPushConstants g_push;

#include "MeshCommon.slangh"
#include "GBufferSurface.slangh"

float4 AxisAngle(float3 axis, float angle) {
    return float4(axis * sin(angle * 0.5), cos(angle * 0.5));
}

// The particle's rotation at a turn of its spin.
float4 Orientation(Particle particle, uint index, float rotation) {
    const float4 base = RotationFromUp(particle.axis);
    if ((particle.state & kParticleResting) != 0) {
        return MultiplyQuaternions(base, AxisAngle(float3(0.0, 1.0, 0.0), rotation));
    }
    // A tumbling axis of its own.
    uint seed = Hash(index * 7919u + 17u);
    const float z = Random(seed) * 2.0 - 1.0;
    const float angle = Random(seed) * 6.2831853;
    const float r = sqrt(max(1.0 - z * z, 0.0));
    return MultiplyQuaternions(base, AxisAngle(float3(r * cos(angle), r * sin(angle), z), rotation));
}

VertexOutput VSMain(VertexInput input, uint instance : SV_VulkanInstanceID) {
    const FrameData frame = g_frames[g_push.frameBuffer][0];
    const uint index = g_particleLists[g_push.list][g_push.listStart + instance];
    const Particle particle = g_particleData[g_push.particles][index];
    const float life = saturate(particle.age / particle.lifetime);
    const float4 scales = SampleCurveIn(g_push.curves, particle.effect, kCurveScales, life);
    const float size = particle.size * scales.x;

    const float4 rotation = Orientation(particle, index, particle.rotation);
    const float3 world = particle.position + Rotate(rotation, input.position * size);
    const bool resting = (particle.state & kParticleResting) != 0;
    const float3 previousCenter = resting ? particle.position : particle.position - particle.velocity * scales.z * g_push.timeStep;
    const float4 previousRotation = Orientation(particle, index, particle.rotation - particle.rotationSpeed * g_push.timeStep);
    const float3 previous = previousCenter + Rotate(previousRotation, input.position * size);

    VertexOutput output;
    output.clipPosition = mul(frame.viewProjection, float4(world, 1.0));
    output.currentClip = mul(frame.unjitteredViewProjection, float4(world, 1.0));
    output.previousClip = mul(frame.previousViewProjection, float4(previous, 1.0));
    output.worldNormal = Rotate(rotation, input.normal);
    output.worldTangent = float4(Rotate(rotation, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    output.worldPosition = world;
    return output;
}
