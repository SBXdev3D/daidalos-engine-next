// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// One iteration of the denoiser's spatial filter (ADR-0039): an à-trous wavelet, 5 by 5 taps spread
// `step` pixels apart, which doubles from one iteration to the next (Dammertz et al. 2010). A neighbour
// counts if it lies on the pixel's surface, faces the same way, and its light differs from the pixel's
// by no more than the noise the variance expects: where the light is not noisy, nothing is blurred
// (Schied et al., "Spatiotemporal Variance-Guided Filtering", 2017). A pixel with a long history needs
// few neighbours: the wider iterations leave it alone. Specular light is blurred less on smoother
// surfaces, and not at all on mirrors.

#define DAI_NO_MESH_PUSH_CONSTANTS
#include "MeshTypes.slangh"
#include "Denoiser.slangh"

struct SpatialPushConstants {
    uint frameBuffer;
    uint depth;
    uint normals; // The G-buffer's normals and roughness.
    uint guides;  // The temporal pass's: the frames each pixel has averaged, in alpha.
    uint inputDiffuse; // Light and the variance of its average, sampled.
    uint inputSpecular;
    uint outputDiffuse; // Storage images.
    uint outputSpecular;
    uint step;
    uint width;
    uint height;
    // For how many frames each sample of the diffuse and of the specular light is reused (ADR-0097).
    uint diffuseRepeats;
    uint specularRepeats;
};

[[vk::push_constant]] SpatialPushConstants g_spatial;

// How far, relative to the camera's distance, a neighbour may be from the pixel's plane.
static const float kPlaneTolerance = 0.01;
// The sharpness of the normals' weight: their cosine to this power.
static const float kNormalPower = 64.0;
// How many standard deviations of noise two lights may differ by and still be blended.
static const float kLuminanceSigma = 4.0;
// Below this roughness, specular light is not blurred: a mirror's reflection has edges.
static const float kSmoothRoughness = 0.1;
// From this roughness, specular light is blurred as much as diffuse light.
static const float kRoughRoughness = 0.4;
// Noise below this share of the light's luminance, one standard deviation, is not filtered: it cannot be
// seen, and most pixels with a long history are below it.
static const float kQuietNoise = 0.005;

float4 Load(uint texture, int2 pixel) {
    return g_textures[texture].Load(int3(pixel, 0));
}

// The variance around a pixel, blurred over 3 by 3 pixels: one pixel's own estimate is too noisy to
// judge its neighbours by.
float2 BlurredVariance(int2 pixel, int2 size) {
    float2 sum = 0.0;
    float total = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), size - 1);
            const float weight = (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
            sum += float2(Load(g_spatial.inputDiffuse, neighbour).a, Load(g_spatial.inputSpecular, neighbour).a) * weight;
            total += weight;
        }
    }
    return sum / total;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const int2 size = int2(g_spatial.width, g_spatial.height);
    if (any(int2(id.xy) >= size)) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float depth = Load(g_spatial.depth, pixel).r;
    const float4 diffuse = Load(g_spatial.inputDiffuse, pixel);
    const float4 specular = Load(g_spatial.inputSpecular, pixel);
    // The average of n samples is as noisy as one sample blurred over n pixels: a pixel's light takes part
    // in an iteration while its kernel, step pixels apart, does not reach beyond what its history lacks. A
    // sample reused for several frames counts once.
    const float frames = Load(g_spatial.guides, pixel).w;
    const float2 samples = max(frames / float2(g_spatial.diffuseRepeats, g_spatial.specularRepeats), 1.0);
    bool2 filter = float(g_spatial.step * g_spatial.step) * samples <= kMaxFrames;
    if (depth <= 0.0 || !any(filter)) {
        g_halfStorageImages[g_spatial.outputDiffuse][pixel] = diffuse;
        g_halfStorageImages[g_spatial.outputSpecular][pixel] = specular;
        return;
    }
    const float4 normalRoughness = LoadNormalRoughness(g_spatial.normals, pixel);
    const float specularSpread = saturate((normalRoughness.w - kSmoothRoughness) / (kRoughRoughness - kSmoothRoughness));
    const float2 deviation = sqrt(BlurredVariance(pixel, size));
    const float diffuseLuminance = Luminance(diffuse.rgb);
    const float specularLuminance = Luminance(specular.rgb);
    // Light whose noise is too faint to see is left as it is, and so is a mirror's reflection, which takes
    // no neighbours: the filter would give back the same light (ADR-0097).
    filter = filter && deviation > kQuietNoise * float2(diffuseLuminance, specularLuminance);
    filter.y = filter.y && specularSpread > 0.0;
    if (!any(filter)) {
        g_halfStorageImages[g_spatial.outputDiffuse][pixel] = diffuse;
        g_halfStorageImages[g_spatial.outputSpecular][pixel] = specular;
        return;
    }
    const FrameData frame = g_frames[g_spatial.frameBuffer][0];
    const float2 extent = float2(size);
    const float3 position = WorldPosition(frame, (float2(pixel) + 0.5) / extent, depth);
    const float planeTolerance = kPlaneTolerance * LinearDepth(frame, depth) * float(g_spatial.step);
    const float3 normal = normalRoughness.xyz;

    // The B3 spline: 1/16, 1/4, 3/8, 1/4, 1/16.
    const float kernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
    float3 diffuseSum = 0.0;
    float3 specularSum = 0.0;
    float2 varianceSum = 0.0;
    float2 weightSum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int2 neighbour = pixel + int2(x, y) * int(g_spatial.step);
            if (any(neighbour < 0) || any(neighbour >= size)) {
                continue;
            }
            const float neighbourDepth = Load(g_spatial.depth, neighbour).r;
            if (neighbourDepth <= 0.0) {
                continue;
            }
            // Only the light being filtered is read.
            const float4 neighbourDiffuse = filter.x ? Load(g_spatial.inputDiffuse, neighbour) : 0.0;
            const float4 neighbourSpecular = filter.y ? Load(g_spatial.inputSpecular, neighbour) : 0.0;
            const float4 neighbourNormal = LoadNormalRoughness(g_spatial.normals, neighbour);
            const float3 neighbourPosition =
                WorldPosition(frame, (float2(neighbour) + 0.5) / extent, neighbourDepth);
            const bool center = x == 0 && y == 0;

            const float geometry = kernel[abs(x)] * kernel[abs(y)] *
                                   exp(-abs(dot(normal, neighbourPosition - position)) / planeTolerance) *
                                   pow(saturate(dot(normal, neighbourNormal.xyz)), kNormalPower);
            const float diffuseWeight =
                geometry * exp(-abs(Luminance(neighbourDiffuse.rgb) - diffuseLuminance) /
                               (kLuminanceSigma * deviation.x + 1e-4));
            const float specularWeight =
                center ? geometry
                       : geometry * specularSpread * exp(-abs(neighbourNormal.w - normalRoughness.w) * 10.0) *
                             exp(-abs(Luminance(neighbourSpecular.rgb) - specularLuminance) /
                                 (kLuminanceSigma * deviation.y + 1e-4));
            diffuseSum += neighbourDiffuse.rgb * diffuseWeight;
            specularSum += neighbourSpecular.rgb * specularWeight;
            varianceSum += float2(neighbourDiffuse.a * diffuseWeight * diffuseWeight,
                                  neighbourSpecular.a * specularWeight * specularWeight);
            weightSum += float2(diffuseWeight, specularWeight);
        }
    }
    // The pixel itself always counts: the sums are never empty.
    g_halfStorageImages[g_spatial.outputDiffuse][pixel] =
        filter.x ? float4(diffuseSum / weightSum.x, varianceSum.x / (weightSum.x * weightSum.x)) : diffuse;
    g_halfStorageImages[g_spatial.outputSpecular][pixel] =
        filter.y ? float4(specularSum / weightSum.y, varianceSum.y / (weightSum.y * weightSum.y)) : specular;
}
