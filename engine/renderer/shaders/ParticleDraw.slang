// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// Draws the sorted particles as quads facing the camera, back to front (ADR-0025). They fade where they
// meet what is behind them (soft particles), are lit by the frame's lights unless they glow, one light
// sampled per pixel with its shadow ray (ADR-0044), and fade into the fog. The output is premultiplied: an additive particle has an alpha of 0, so one blend
// mode draws both kinds.

#define DAI_PARTICLES_READ_ONLY
#include "Particles.slangh"
#include "RayQueries.slangh"
#include "Lights.slangh"

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float3 worldPosition : POSITION;
    [[vk::location(3)]] nointerpolation uint effect : EFFECT;
};

VertexOutput VSMain(uint vertexId : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID) {
    const Particle particle = g_particleData[g_particles.particles][g_particleKeys[g_particles.keys][instance].y];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][particle.effect];
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const float life = saturate(particle.age / particle.lifetime);

    // Two triangles: corners from -1 to 1, turned by the particle's rotation.
    static const float2 kCorners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1),
                                       float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 corner = kCorners[vertexId];
    const float c = cos(particle.rotation);
    const float s = sin(particle.rotation);
    const float2 turned = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
    const float halfSize = particle.size * lerp(1.0, effect.growth, life) * 0.5;
    const float3 world =
        particle.position + (g_particles.cameraRight * turned.x + g_particles.cameraUp * turned.y) * halfSize;

    VertexOutput output;
    output.clipPosition = mul(frame.viewProjection, float4(world, 1.0));
    // The effect's cell of the atlas; rows of the image go down.
    const float2 cellSize = 1.0 / float(effect.columns);
    const float2 cell = float2(effect.cell % effect.columns, effect.cell / effect.columns);
    output.uv = (cell + (corner * float2(0.5, -0.5) + 0.5)) * cellSize;
    output.color = lerp(effect.startColor, effect.endColor, life);
    // Fades in over the first tenth of its life, so that a stream does not start with a hard edge.
    output.color.a *= saturate(life * 10.0);
    output.worldPosition = world;
    output.effect = particle.effect;
    return output;
}

struct ParticleOutput {
    float4 color : SV_Target0;
    // Its coverage, blended over the others' like the color: the reactive mask of temporal antialiasing.
    float4 reactive : SV_Target1;
};

ParticleOutput Output(float4 color, float coverage) {
    ParticleOutput output;
    output.color = color;
    output.reactive = float4(coverage, 0.0, 0.0, coverage);
    return output;
}

ParticleOutput PSMain(VertexOutput input) {
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][input.effect];
    const float4 texel = g_textures[effect.texture].Sample(g_samplers[kSamplerLinearClamp], input.uv);
    float alpha = input.color.a * texel.a;

    // Hidden behind what the meshes drew, and fading as it gets close to it.
    const float depth = dot(input.worldPosition - frame.cameraPosition, frame.cameraForward);
    const float sceneDepth =
        LinearDepth(frame, g_textures[g_particles.depthTexture].Load(int3(int2(input.clipPosition.xy), 0)).r);
    alpha *= saturate((sceneDepth - depth) / max(effect.softness, 0.001));
    if (alpha <= 0.001) {
        discard;
    }

    float3 color = input.color.rgb * texel.rgb;
    if ((effect.flags & kEffectLit) != 0) {
        // Smoke takes the light around it, from every side, as a white matte surface facing each light: its
        // illuminance over pi (ADR-0043), with shadows. The indirect light is that of the surfaces behind it,
        // averaged over a few pixels around, which also smooths its noise (ADR-0040).
        float3 light = float3(0.0, 0.0, 0.0);
        const int2 pixel = int2(input.clipPosition.xy);
        if (frame.indirectTexture != kNoTexture) {
            const int2 last = int2(1.0 / frame.screenTexel) - 1;
            for (int tap = 0; tap < 4; ++tap) {
                const int2 offset = int2((tap & 1) != 0 ? 6 : -6, (tap & 2) != 0 ? 6 : -6);
                light += g_textures[frame.indirectTexture].Load(int3(clamp(pixel + offset, int2(0, 0), last), 0)).rgb * 0.25;
            }
        }
        Receiver receiver;
        receiver.position = input.worldPosition;
        receiver.origin = input.worldPosition;
        receiver.faceNormal = -frame.cameraForward;
        receiver.normal = receiver.faceNormal;
        receiver.view = receiver.faceNormal;
        receiver.diffuseColor = float3(1.0, 1.0, 1.0);
        receiver.f0 = float3(0.0, 0.0, 0.0);
        receiver.roughness = 1.0;
        receiver.response = kRespondEverywhere;
        uint random = Pcg(uint(pixel.y) * 65521u + uint(pixel.x)) ^ Pcg(frame.frameIndex * 9781u + input.effect);
        light += SampleDirectLight(frame, receiver, random, g_particles.instanceBuffer, g_particles.materialBuffer);
        color *= light;
    }

    const float visibility = exp(-frame.fogDensity * max(depth, 0.0));
    const bool additive = (effect.flags & kEffectAdditive) != 0;
    if (additive) {
        return Output(float4(color * alpha * visibility, 0.0), alpha);
    }
    return Output(float4(lerp(frame.fogColor, color, visibility) * alpha, alpha), alpha);
}
