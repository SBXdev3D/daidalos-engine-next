// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The lighting pass (ADR-0044), ReSTIR DI's last: the light each pixel's reservoir chose, through one
// shadow ray, times the weight that makes it an estimate of every light, plus the indirect light and the
// reflections, which the material's occlusion darkens, as direct light has its shadows. It writes the light
// apart from the surface's colors, diffuse and specular, for the denoiser (ADR-0039); the composite puts
// them back together. A chosen light that the shadow ray finds hidden is dropped from the reservoir, so that
// the next frame does not reuse it.
// With kRestirDebug, it also shows each pixel's light, one hue per light; with kRestirLightCount too, how many
// of the frame's lights reach it (ADR-0106): blue for one, through green and yellow, red for 16 or more.

#include "Restir.slangh"

float3 Hue(uint light) {
    const float hue = frac(float(Pcg(light + 1u) & 0xFFFF) / 65536.0);
    return saturate(abs(frac(hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

// The analytic lights whose range reaches a point, whatever their shadows.
uint LightsReaching(FrameData frame, float3 position) {
    uint count = 0;
    for (uint index = 0; index < frame.lightCount; ++index) {
        const LightData light = LightAt(g_lights[frame.lightBuffer][index], position);
        if (distance(light.position, position) < light.range) {
            ++count;
        }
    }
    return count;
}

// Black for none, blue for one, then green, yellow and red at 16 and more.
float3 Heat(uint count) {
    if (count == 0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float t = saturate(float(count - 1) / 15.0);
    return saturate(float3(t * 2.0 - 0.5, 1.5 - abs(t * 2.0 - 1.0) * 1.5, 1.0 - t * 2.0)) * 0.9 + 0.1;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(g_restir.width, g_restir.height))) {
        return;
    }
    const FrameData frame = g_frames[g_restir.frameBuffer][0];
    const int2 pixel = int2(id.xy);
    const GBufferSurface surface = ReadGBuffer(frame, Textures(), pixel);
    if (!surface.found) {
        g_storageImages[g_restir.diffuse][id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        g_storageImages[g_restir.specular][id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        if ((g_restir.flags & kRestirDebug) != 0) {
            g_storageImages[g_restir.debug][id.xy] = float4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }
    const Receiver receiver = ReceiverOf(frame, surface);
    const float3 specularAlbedo =
        LayeredSpecularAlbedo(surface.f0, surface.roughness, max(dot(surface.normal, surface.view), 0.0), surface.layers);
    float3 diffuseLight = float3(0.0, 0.0, 0.0);
    float3 specularLight = float3(0.0, 0.0, 0.0);
    // Where the light may be noisy, so that the denoiser filters it: one light chosen among several, a light
    // with a size, the indirect light.
    bool stochastic = frame.sourceCount > 1;

    if ((g_restir.flags & kRestirIndirect) != 0 && frame.indirectTexture != kNoTexture) {
        const int3 texel = int3(pixel, 0);
        const float3 indirect = g_textures[frame.indirectTexture].Load(texel).rgb * surface.occlusion;
        diffuseLight += indirect;
        specularLight += (frame.reflectionTexture != kNoTexture
                              ? g_textures[frame.reflectionTexture].Load(texel).rgb * surface.occlusion
                              : indirect) *
                         specularAlbedo;
        stochastic = true;
    }

    const uint index = PixelIndex(pixel);
    Reservoir reservoir = Unpack(g_reservoirs[g_restir.reservoirs][index]);
    // The light chosen moved since the last frame, as a flashlight does with the camera: the denoiser keeps
    // little of its history, which would lag behind (ADR-0051).
    bool moving = false;
    if (reservoir.light != kNoLight) {
        const LightContact contact = ContactLight(frame, reservoir.light, reservoir.u, receiver);
        stochastic = stochastic || contact.soft;
        moving = (g_lightMaps[g_restir.lightMap][g_restir.previousLightCount + reservoir.light] & kLightMoved) != 0;
        if (contact.valid && Unoccluded(frame, receiver, contact, g_restir.instanceBuffer, g_restir.materialBuffer)) {
            const BsdfTerms terms = LightTerms(receiver, contact);
            diffuseLight += contact.illuminance * terms.diffuse * reservoir.weight;
            specularLight += contact.illuminance * terms.specular * reservoir.weight;
        } else {
            reservoir.light = kNoLight;
            reservoir.weight = 0.0;
            g_reservoirs[g_restir.reservoirs][index] = Pack(reservoir);
        }
    }

    // Divided by the albedo's luminance, which keeps the color of a metal's reflections in the light.
    const float albedo = max(Luminance(specularAlbedo), 0.001);
    g_storageImages[g_restir.diffuse][id.xy] = float4(diffuseLight, (stochastic ? 1.0 : 0.0) + (moving ? 2.0 : 0.0));
    g_storageImages[g_restir.specular][id.xy] = float4(specularLight / albedo, albedo);
    if ((g_restir.flags & kRestirDebug) != 0) {
        const float3 shown = (g_restir.flags & kRestirLightCount) != 0
                                 ? Heat(LightsReaching(frame, surface.position))
                                 : (reservoir.light != kNoLight ? Hue(reservoir.light) : float3(0.0, 0.0, 0.0));
        g_storageImages[g_restir.debug][id.xy] = float4(shown, 1.0);
    }
}
