// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Draws the sorted particles as quads, back to front (ADR-0025), turned, animated and colored as their
// effect says (ADR-0053). They fade where they meet what is behind them (soft particles), are lit by the
// frame's lights unless they glow, with the light the simulation keeps for each particle (ADR-0044), and fade
// into the fog. The output is premultiplied: an additive particle has an alpha of 0, so one blend mode draws
// both kinds.

#define DAI_PARTICLES_READ_ONLY
#include "Particles.slangh"
#include "Fog.slangh"

struct VertexOutput {
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float4 uv : TEXCOORD; // This frame's cell, and the next's.
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float3 worldPosition : POSITION;
    [[vk::location(3)]] nointerpolation uint effect : EFFECT;
    [[vk::location(4)]] nointerpolation float3 light : LIGHT;
    [[vk::location(5)]] nointerpolation float blend : BLEND; // Of the next frame.
};

// The frames of the flipbook a particle is between, and how far it is from the first to the second. At a
// frame rate, the flipbook loops or holds its last frame, as the effect says; over the life, it plays once,
// or wraps around from a particle's own start frame.
struct Flipbook {
    uint current;
    uint next;
    float blend;
};
Flipbook FlipbookAt(ParticleEffectData effect, Particle particle, float life) {
    const uint count = max(effect.frames, 1);
    const float frames = float(count);
    const bool loops = effect.frameRate > 0.0 ? (effect.flags & kEffectLoop) != 0 : particle.startFrame > 0.0;
    float frame = particle.startFrame + (effect.frameRate > 0.0 ? particle.age * effect.frameRate : life * frames);
    frame = loops ? frame - frames * floor(frame / frames) : min(frame, frames - 1.0);
    Flipbook flipbook;
    flipbook.current = min(uint(frame), count - 1);
    flipbook.next = loops ? (flipbook.current + 1) % count : min(flipbook.current + 1, count - 1);
    flipbook.blend = frame - floor(frame);
    return flipbook;
}

// The corner of a cell of the grid, as texture coordinates; rows of the image go down.
float2 CellUv(ParticleEffectData effect, uint cell, float2 corner) {
    const uint columns = max(effect.columns, 1);
    const uint rows = effect.rows > 0 ? effect.rows : columns;
    const float2 grid = float2(float(columns), float(rows));
    return (float2(float(cell % columns), float((cell / columns) % rows)) + (corner * float2(0.5, -0.5) + 0.5)) / grid;
}

VertexOutput VSMain(uint vertexId : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID) {
    const Particle particle = g_particleData[g_particles.particles][g_particleKeys[g_particles.keys][instance].y];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][particle.effect];
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const float life = saturate(particle.age / particle.lifetime);
    const float4 scales = SampleCurve(particle.effect, kCurveScales, life);

    // Two triangles: corners from -1 to 1.
    static const float2 kCorners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1),
                                       float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 corner = kCorners[vertexId];
    const float halfSize = particle.size * scales.x * 0.5;
    const float c = cos(particle.rotation);
    const float s = sin(particle.rotation);
    const float2 turned = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
    const float3 toCamera = normalize(frame.cameraPosition - particle.position + 0.0001);

    // The quad's sides, each as long as half its extent (renderer::ParticleOrientation).
    float3 right = g_particles.cameraRight * halfSize;
    float3 up = g_particles.cameraUp * halfSize;
    float2 along = turned;
    const float3 velocity = particle.velocity * scales.z;
    const float speed = length(velocity);
    if (effect.orientation == kOrientVelocity && speed > 1.0e-4) {
        // Its length along the motion, its width across it, facing the camera as much as it can.
        const float3 direction = velocity / speed;
        const float3 side = cross(direction, toCamera);
        if (dot(side, side) > 1.0e-6) {
            right = normalize(side) * halfSize;
            up = direction * (halfSize + 0.5 * speed * effect.stretch);
            along = corner;
        }
    } else if (effect.orientation == kOrientAxis) {
        const float3 side = cross(particle.axis, toCamera);
        if (dot(side, side) > 1.0e-6) {
            right = normalize(side) * halfSize;
            up = particle.axis * halfSize;
            along = corner;
        }
    } else if (effect.orientation == kOrientPlane) {
        const float3 helper = abs(particle.axis.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        const float3 side = normalize(cross(helper, particle.axis));
        right = side * halfSize;
        up = cross(particle.axis, side) * halfSize;
    }
    const float3 world = particle.position + right * along.x + up * along.y;

    VertexOutput output;
    output.clipPosition = mul(frame.viewProjection, float4(world, 1.0));
    // This frame of the flipbook and the next, blended.
    const Flipbook flipbook = FlipbookAt(effect, particle, life);
    output.uv = float4(CellUv(effect, effect.cell + flipbook.current, corner), CellUv(effect, effect.cell + flipbook.next, corner));
    output.blend = flipbook.blend;
    output.color = SampleCurve(particle.effect, kCurveColor, life);
    output.color.rgb *= scales.y;
    // Fades in over the start of its life, so that a stream does not start with a hard edge.
    output.color.a *= effect.fadeIn > 0.0 ? saturate(life / effect.fadeIn) : 1.0;
    output.worldPosition = world;
    output.effect = particle.effect;
    output.light = particle.light;
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
    const float4 texel = lerp(g_textures[effect.texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.xy),
                              g_textures[effect.texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.zw), input.blend);
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
        // Smoke takes the light around it, from every side: the frame's lights, as the simulation found them
        // at the particle (ADR-0044), and the indirect light of the surfaces behind it, averaged over a few
        // pixels around, which also smooths its noise (ADR-0040).
        float3 light = input.light;
        const int2 pixel = int2(input.clipPosition.xy);
        if (frame.indirectTexture != kNoTexture) {
            const int2 last = int2(1.0 / frame.screenTexel) - 1;
            for (int tap = 0; tap < 4; ++tap) {
                const int2 offset = int2((tap & 1) != 0 ? 6 : -6, (tap & 2) != 0 ? 6 : -6);
                light += g_textures[frame.indirectTexture].Load(int3(clamp(pixel + offset, int2(0, 0), last), 0)).rgb * 0.25;
            }
        }
        color *= light;
    }

    // Seen through the fog in front of it (ADR-0051).
    const float4 fog =
        FogAt(frame, input.clipPosition.xy * frame.screenTexel, max(depth, 0.0), uint2(input.clipPosition.xy));
    const bool additive = (effect.flags & kEffectAdditive) != 0;
    if (additive) {
        return Output(float4(color * alpha * fog.a, 0.0), alpha);
    }
    return Output(float4(ThroughFog(fog, color) * alpha, alpha), alpha);
}
