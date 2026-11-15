// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Draws the sorted particles as quads, back to front (ADR-0025), turned, animated and colored as their
// effect says (ADR-0053). They fade where they meet what is behind them (soft particles), are lit by the
// frame's lights unless they glow, with the light the simulation keeps for each particle (ADR-0044), and fade
// into the fog. The output is premultiplied: an additive particle has an alpha of 0, so one blend mode draws
// both kinds.
// The sorted list also holds the frame's ribbon segments (ADR-0053), each a quad between two of its ribbon's
// points, turned to the camera around the ribbon; they glow, fade into surfaces and into the fog.

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
    // Towards the particle's light, in the frame of its image: right, up, and out of it towards the camera.
    [[vk::location(6)]] nointerpolation float3 lightInImage : LIGHTINIMAGE;
    [[vk::location(7)]] float2 corner : CORNER; // From -1 to 1 across the quad.
    [[vk::location(8)]] nointerpolation uint ribbon : RIBBON; // A ribbon's segment's, if effect is kNoEffect.
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

// Two triangles: corners from -1 to 1.
static const float2 kCorners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1),
                                   float2(-1, -1), float2(1, 1), float2(-1, 1)};

// A segment of a ribbon: from its first point (corner.y -1) to the next (1), across by its width.
VertexOutput RibbonSegment(uint segment, uint vertexId) {
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const ParticleFrame particles = g_particleFrames[g_particles.parameters][0];
    const float2 corner = kCorners[vertexId];
    const uint first = g_ribbonSegments[particles.ribbonSegments][segment];
    const RibbonPointData point = g_ribbonPoints[particles.ribbonPoints][corner.y < 0.0 ? first : first + 1];
    const float3 toCamera = normalize(frame.cameraPosition - point.position + 0.0001);
    float3 side = cross(point.tangent, toCamera);
    side = dot(side, side) > 1.0e-8 ? normalize(side) : g_particles.cameraRight;
    const float3 world = point.position + side * (point.width * 0.5 * corner.x);

    VertexOutput output;
    output.clipPosition = mul(frame.viewProjection, float4(world, 1.0));
    output.uv = float4(point.u, corner.x * 0.5 + 0.5, 0.0, 0.0);
    output.color = point.color;
    output.worldPosition = world;
    output.effect = kNoEffect;
    output.light = float3(0.0, 0.0, 0.0);
    output.blend = 0.0;
    output.lightInImage = float3(0.0, 0.0, 0.0);
    output.corner = corner;
    output.ribbon = point.ribbon;
    return output;
}

VertexOutput VSMain(uint vertexId : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID) {
    const uint2 key = g_particleKeys[g_particles.keys][instance];
    if ((key.y & kRibbonKey) != 0) {
        return RibbonSegment(key.y & ~kRibbonKey, vertexId);
    }
    const Particle particle = g_particleData[g_particles.particles][key.y];
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][particle.effect];
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    const float life = saturate(particle.age / particle.lifetime);
    const float4 scales = SampleCurve(particle.effect, kCurveScales, life);

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
    // The image's axes in the world: turned with the particle when its quad is.
    const bool turns = effect.orientation == kOrientCamera || effect.orientation == kOrientPlane ||
                       (effect.orientation == kOrientVelocity && speed <= 1.0e-4);
    const float3 imageX = normalize(turns ? right * c + up * s : right);
    const float3 imageY = normalize(turns ? up * c - right * s : up);
    const float3 imageZ = normalize(cross(imageX, imageY));

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
    const float3 toLight = particle.lightDirection;
    output.lightInImage = float3(dot(toLight, imageX), dot(toLight, imageY), dot(toLight, imageZ));
    output.corner = corner;
    output.ribbon = 0;
    return output;
}

struct ParticleOutput {
    float4 color : SV_Target0;
    // Its coverage, blended over the others' like the color: the reactive mask of temporal antialiasing.
    float4 reactive : SV_Target1;
    // How far the frame behind it is bent, as a fraction of the frame's height (ADR-0053): heat haze adds
    // to it, and what covers the haze hides it, with the same blending as the color.
    float4 distortion : SV_Target2;
};

ParticleOutput Output(float4 color, float coverage) {
    ParticleOutput output;
    output.color = color;
    output.reactive = float4(coverage, 0.0, 0.0, coverage);
    output.distortion = float4(0.0, 0.0, 0.0, color.a);
    return output;
}

// A sample of a six-way light map at the particle's two frames of the flipbook.
float3 SampleBoth(uint texture, VertexOutput input) {
    return lerp(g_textures[texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.xy),
                g_textures[texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.zw), input.blend).rgb;
}

// Hidden behind what the meshes drew, and fading as it gets close to it.
float SoftAlpha(FrameData frame, VertexOutput input, float softness, out float depth) {
    depth = dot(input.worldPosition - frame.cameraPosition, frame.cameraForward);
    const float sceneDepth =
        LinearDepth(frame, g_textures[g_particles.depthTexture].Load(int3(int2(input.clipPosition.xy), 0)).r);
    return saturate((sceneDepth - depth) / max(softness, 0.001));
}

// What a ribbon gives: its texture, repeated along it, times its color; it glows, through the fog.
ParticleOutput RibbonPixel(FrameData frame, VertexOutput input) {
    const ParticleFrame particles = g_particleFrames[g_particles.parameters][0];
    const RibbonData ribbon = g_ribbons[particles.ribbons][input.ribbon];
    const float4 texel = ribbon.texture != kNoTexture
                             ? g_textures[ribbon.texture].Sample(g_samplers[kSamplerLinearRepeat], input.uv.xy)
                             : float4(1.0, 1.0, 1.0, 1.0);
    float depth;
    const float alpha = input.color.a * texel.a * SoftAlpha(frame, input, ribbon.softness, depth);
    if (alpha <= 0.001) {
        discard;
    }
    const float3 color = input.color.rgb * texel.rgb;
    const float4 fog =
        FogAt(frame, input.clipPosition.xy * frame.screenTexel, max(depth, 0.0), uint2(input.clipPosition.xy));
    if ((ribbon.flags & kEffectAdditive) != 0) {
        return Output(float4(color * alpha * fog.a, 0.0), alpha);
    }
    return Output(float4(ThroughFog(fog, color) * alpha, alpha), alpha);
}

ParticleOutput PSMain(VertexOutput input) {
    const FrameData frame = g_frames[g_particles.frameBuffer][0];
    if (input.effect == kNoEffect) {
        return RibbonPixel(frame, input);
    }
    const ParticleEffectData effect = g_particleEffects[g_particles.effects][input.effect];
    const float4 texel = lerp(g_textures[effect.texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.xy),
                              g_textures[effect.texture].Sample(g_samplers[kSamplerLinearClamp], input.uv.zw), input.blend);
    float alpha = input.color.a * texel.a;

    float depth;
    alpha *= SoftAlpha(frame, input, effect.softness, depth);
    if (alpha <= 0.001) {
        discard;
    }

    if (effect.distortion > 0.0) {
        // Heat haze: it bends the frame behind it with ripples that rise, and draws nothing.
        const float time = frame.time;
        const float2 p = input.corner * 1.5 + float2(0.0, -time * 1.2);
        const float2 ripple = float2(sin(p.y * 7.3 + sin(p.x * 5.1 + time * 2.3)), cos(p.x * 6.7 + sin(p.y * 4.3 - time * 1.9)));
        ParticleOutput output = Output(float4(0.0, 0.0, 0.0, 0.0), alpha);
        output.distortion = float4(ripple * effect.distortion * alpha, 0.0, 0.0);
        return output;
    }

    float3 color = input.color.rgb * texel.rgb;
    if ((effect.flags & kEffectLit) != 0) {
        // Smoke takes the light around it, from every side: the frame's lights, as the simulation found them
        // at the particle (ADR-0044), and the indirect light of the surfaces behind it, averaged over a few
        // pixels around, which also smooths its noise (ADR-0040).
        float3 direct = input.light;
        float3 indirect = float3(0.0, 0.0, 0.0);
        const int2 pixel = int2(input.clipPosition.xy);
        if (frame.indirectTexture != kNoTexture) {
            const int2 last = int2(1.0 / frame.screenTexel) - 1;
            for (int tap = 0; tap < 4; ++tap) {
                const int2 offset = int2((tap & 1) != 0 ? 6 : -6, (tap & 2) != 0 ? 6 : -6);
                indirect += g_textures[frame.indirectTexture].Load(int3(clamp(pixel + offset, int2(0, 0), last), 0)).rgb * 0.25;
            }
        }
        if (effect.lightPositive != kNoTexture && effect.lightNegative != kNoTexture) {
            // Six-way light maps (ADR-0053): how much of a light from each side of the image reaches the eye,
            // weighed by the squares of the light's direction, so that they add up to one for any direction.
            // The indirect light, from everywhere, takes their average.
            const float3 positive = SampleBoth(effect.lightPositive, input);
            const float3 negative = SampleBoth(effect.lightNegative, input);
            const float3 towards = saturate(input.lightInImage);
            const float3 away = saturate(-input.lightInImage);
            direct *= dot(towards * towards, positive) + dot(away * away, negative);
            indirect *= dot(positive + negative, float3(1.0, 1.0, 1.0)) / 6.0;
        }
        color *= direct + indirect;
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
