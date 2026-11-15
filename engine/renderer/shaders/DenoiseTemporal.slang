// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The denoiser's first pass (ADR-0039): adds this frame's diffuse and specular light to their history,
// found where the motion vectors say each pixel was, if the surface there is the same one. It counts the
// frames each pixel has averaged, and estimates how noisy one frame's light is from the moments of its
// luminance, or, while there are too few frames, from its neighbours (Schied et al., "Spatiotemporal
// Variance-Guided Filtering", 2017). The average it writes is the next frame's history, unfiltered, so
// that the spatial filter never blurs it twice.
// A reflection does not move with its surface: on smooth surfaces, the specular history is found where the
// reflected point was, the point as far behind the surface as what it reflects is in front of it, from the
// reflections' hit distance (ADR-0042).

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "Denoiser.slangh"

struct TemporalPushConstants {
    uint frameBuffer;
    uint diffuse;  // This frame's light, from the lighting pass.
    uint specular;
    uint depth;
    uint normals;  // The G-buffer's normals and roughness.
    uint motion;
    uint previousGuides;   // Last frame's guides, moments and histories, sampled.
    uint previousMoments;
    uint previousDiffuse;
    uint previousSpecular;
    uint guides;   // Storage images: linear depth, the normal, and the frames averaged.
    uint moments;  // The luminance and its square, of the diffuse then the specular light.
    uint historyDiffuse; // The averaged light, and the variance of the average.
    uint historySpecular;
    uint reset;    // 1 when there is no history.
    uint reflections; // The reflections, with their hit distance in alpha, or kNoTexture.
    // Two numbers rather than a uint2, which would be aligned to 8 bytes.
    uint width;
    uint height;
};

[[vk::push_constant]] TemporalPushConstants g_temporal;

// Below this many frames, the variance comes from the neighbours.
static const float kFramesForVariance = 4.0;
// The moments forget faster than the light, so that the variance follows changes.
static const float kMomentsWeight = 0.2;
// How far the depth of last frame's surface may be from where this one was, relative to it.
static const float kDepthTolerance = 0.05;
// The cosine of the largest angle between last frame's normal and this one.
static const float kNormalTolerance = 0.9;
// Below this roughness, a reflection moves wholly with the reflected point; from the second, with the
// surface; in between, partly with each.
static const float kVirtualRoughness = 0.1;
static const float kSurfaceRoughness = 0.4;

float4 Load(uint texture, int2 pixel) {
    return g_textures[texture].Load(int3(pixel, 0));
}

float4 LumaMoments(float3 diffuse, float3 specular) {
    const float diffuseLuminance = Luminance(diffuse);
    const float specularLuminance = Luminance(specular);
    return float4(diffuseLuminance, diffuseLuminance * diffuseLuminance, specularLuminance,
                  specularLuminance * specularLuminance);
}

// Last frame's light around previousUv, from the four pixels around it that show the same surface: about as
// far from the camera as this one, then or now, and facing the same way. Returns the weight found.
struct History {
    float3 diffuse;
    float3 specular;
    float4 moments;
    float frames;
    float weight;
};

History FindHistory(float2 previousUv, float2 extent, float expectedDepth, float linearDepth, float3 normal) {
    History history;
    history.diffuse = 0.0;
    history.specular = 0.0;
    history.moments = 0.0;
    history.frames = 0.0;
    history.weight = 0.0;
    if (g_temporal.reset != 0 || any(previousUv < 0.0) || any(previousUv > 1.0)) {
        return history;
    }
    const int2 size = int2(extent);
    const float2 texel = previousUv * extent - 0.5;
    const int2 corner = int2(floor(texel));
    const float2 f = texel - float2(corner);
    for (int tap = 0; tap < 4; ++tap) {
        const int2 offset = int2(tap & 1, tap >> 1);
        const int2 previous = corner + offset;
        if (any(previous < 0) || any(previous >= size)) {
            continue;
        }
        const float4 guide = Load(g_temporal.previousGuides, previous);
        const float depthError = min(abs(guide.x - expectedDepth), abs(guide.x - linearDepth));
        if (guide.x <= 0.0 || depthError > kDepthTolerance * linearDepth ||
            dot(DecodeNormal(guide.yz), normal) < kNormalTolerance) {
            continue;
        }
        const float2 bilinear = lerp(1.0 - f, f, float2(offset));
        const float weight = bilinear.x * bilinear.y;
        history.diffuse += Load(g_temporal.previousDiffuse, previous).rgb * weight;
        history.specular += Load(g_temporal.previousSpecular, previous).rgb * weight;
        history.moments += Load(g_temporal.previousMoments, previous) * weight;
        history.frames += guide.w * weight;
        history.weight += weight;
    }
    return history;
}

float2 Variance(float4 moments) {
    return max(float2(moments.y - moments.x * moments.x, moments.w - moments.z * moments.z), 0.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 size = uint2(g_temporal.width, g_temporal.height);
    if (any(id.xy >= size)) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float depth = Load(g_temporal.depth, pixel).r;
    if (depth <= 0.0) {
        // Nothing was drawn: no light, and nothing for the next frame to find.
        g_halfStorageImages[g_temporal.guides][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_halfStorageImages[g_temporal.moments][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_halfStorageImages[g_temporal.historyDiffuse][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        g_halfStorageImages[g_temporal.historySpecular][pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const FrameData frame = g_frames[g_temporal.frameBuffer][0];
    const float2 extent = float2(size);
    const float2 uv = (float2(pixel) + 0.5) / extent;
    const float3 position = WorldPosition(frame, uv, depth);
    const float linearDepth = LinearDepth(frame, depth);
    const float4 normalRoughness = LoadNormalRoughness(g_temporal.normals, pixel);
    const float3 normal = normalRoughness.xyz;
    const float4 diffuse = Load(g_temporal.diffuse, pixel);
    const float3 specular = Load(g_temporal.specular, pixel).rgb;
    // The lighting's flags: shadow rays aimed at random points of a light, so the light may be noisy; and the
    // light chosen moved since the last frame, so the history lags behind it.
    const uint flags = uint(diffuse.a + 0.5);
    const bool stochastic = (flags & 1u) != 0;
    const bool moving = (flags & 2u) != 0;

    // The neighbourhood: its range of light bounds the history, so that light that goes out or comes on
    // shows at once; and its moments, from the neighbours on the same surface, give the variance while
    // the history is short.
    float3 lowDiffuse = diffuse.rgb;
    float3 highDiffuse = diffuse.rgb;
    float3 lowSpecular = specular;
    float3 highSpecular = specular;
    float4 spatialMoments = 0.0;
    float spatialWeight = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), int2(size) - 1);
            const float neighbourDepth = Load(g_temporal.depth, neighbour).r;
            if (neighbourDepth <= 0.0) {
                continue;
            }
            const float3 neighbourDiffuse = Load(g_temporal.diffuse, neighbour).rgb;
            const float3 neighbourSpecular = Load(g_temporal.specular, neighbour).rgb;
            lowDiffuse = min(lowDiffuse, neighbourDiffuse);
            highDiffuse = max(highDiffuse, neighbourDiffuse);
            lowSpecular = min(lowSpecular, neighbourSpecular);
            highSpecular = max(highSpecular, neighbourSpecular);
            if (abs(LinearDepth(frame, neighbourDepth) - linearDepth) < kDepthTolerance * linearDepth) {
                spatialMoments += LumaMoments(neighbourDiffuse, neighbourSpecular);
                spatialWeight += 1.0;
            }
        }
    }

    // Last frame's surface, where the motion vectors say this one was.
    const float2 previousUv = uv + Load(g_temporal.motion, pixel).xy;
    const float expectedDepth = mul(frame.previousViewProjection, float4(position, 1.0)).w;
    // Last frame drew a corner of its own size (ADR-0049).
    const float2 previousExtent = float2(frame.previousRenderSize);
    const History history = FindHistory(previousUv, previousExtent, expectedDepth, linearDepth, normal);
    float3 historyDiffuse = history.diffuse;
    float3 historySpecular = history.specular;
    float4 historyMoments = history.moments;
    float historyFrames = history.frames;
    float historyWeight = history.weight;
    // On a smooth surface, the reflection's history is where the reflected point was.
    const float hitDistance = g_temporal.reflections != kNoTexture ? Load(g_temporal.reflections, pixel).a : 0.0;
    const float virtualShare =
        hitDistance > 0.0 ? saturate((kSurfaceRoughness - normalRoughness.a) / (kSurfaceRoughness - kVirtualRoughness)) : 0.0;
    // A small share moves the history by less than a pixel: not worth a second search.
    if (virtualShare > 0.25 && historyWeight > 0.01) {
        const float3 reflected = position + normalize(position - frame.cameraPosition) * hitDistance;
        const float2 virtualUv = ClipToUv(mul(frame.previousViewProjection, float4(reflected, 1.0)));
        const float2 specularUv = lerp(previousUv, virtualUv, virtualShare);
        const History virtualHistory = FindHistory(specularUv, previousExtent, expectedDepth, linearDepth, normal);
        // Nowhere to find it: the reflection starts over, with the rest of the history.
        historySpecular = virtualHistory.weight > 0.01 ? virtualHistory.specular / virtualHistory.weight * historyWeight
                                                       : specular * historyWeight;
    }

    float frames = 1.0;
    float3 averageDiffuse = diffuse.rgb;
    float3 averageSpecular = specular;
    float4 moments = LumaMoments(diffuse.rgb, specular);
    if (historyWeight > 0.01) {
        frames = min(round(historyFrames / historyWeight) + 1.0, moving ? kMovingLightFrames : kMaxFrames);
        const float weight = 1.0 / frames;
        // The history is kept within the neighbourhood's range, widened by the noise of one frame until
        // now: where the light was not noisy, a light that went out or a shadow that moved away shows at
        // once; in a penumbra, whose neighbours are often all lit or all dark in one frame, the history
        // stays.
        const float2 deviation = sqrt(Variance(historyMoments / historyWeight));
        moments = lerp(historyMoments / historyWeight, moments, max(weight, kMomentsWeight));
        const float3 keptDiffuse =
            clamp(historyDiffuse / historyWeight, lowDiffuse - deviation.x, highDiffuse + deviation.x);
        const float3 keptSpecular =
            clamp(historySpecular / historyWeight, lowSpecular - deviation.y, highSpecular + deviation.y);
        averageDiffuse = lerp(keptDiffuse, diffuse.rgb, weight);
        averageSpecular = lerp(keptSpecular, specular, weight);
    }

    // The variance of one frame's light; light that is not noisy needs no filtering, and its edges, such as
    // hard shadows, stay sharp.
    float2 variance = Variance(moments);
    if (frames < kFramesForVariance) {
        variance = stochastic ? Variance(spatialMoments / max(spatialWeight, 1.0)) : 0.0;
    }

    g_halfStorageImages[g_temporal.guides][pixel] = float4(linearDepth, EncodeNormal(normal), frames);
    g_halfStorageImages[g_temporal.moments][pixel] = moments;
    // The average of the frames has a variance that many times smaller than one frame's.
    g_halfStorageImages[g_temporal.historyDiffuse][pixel] = float4(averageDiffuse, variance.x / frames);
    g_halfStorageImages[g_temporal.historySpecular][pixel] = float4(averageSpecular, variance.y / frames);
}
