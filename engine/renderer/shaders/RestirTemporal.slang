// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// ReSTIR DI's temporal reuse (ADR-0044): each pixel finds its surface in the last frame, through its motion
// vector, where the last frame's face normal and depth agree, and combines its new reservoir with the one kept
// there, its light mapped to this frame's list. The history stands for at most kMaxHistory frames, so that
// the past fades. Both reservoirs belong to the same surface, so they are weighed by their confidence alone
// (generalized RIS, Lin et al. 2022), but for lights that are new this frame, which only the new reservoir can
// have found. Biased about visibility by default, as the spatial reuse is: a history that holds no light is
// passed over.

#include "Restir.slangh"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(g_restir.width, g_restir.height))) {
        return;
    }
    const FrameData frame = g_frames[g_restir.frameBuffer][0];
    const int2 pixel = int2(id.xy);
    const GBufferSurface surface = ReadGBuffer(frame, Textures(), pixel);
    if (!surface.found || (g_restir.flags & kRestirTemporal) == 0) {
        return; // The new reservoir stays as it is.
    }
    const float2 motion = g_textures[g_restir.motion].Load(int3(pixel, 0)).rg;
    const float2 previousUv = (float2(pixel) + 0.5) * frame.screenTexel + motion;
    if (any(previousUv < 0.0) || any(previousUv >= 1.0)) {
        return;
    }
    const int2 previous = int2(previousUv * float2(frame.previousRenderSize));
    const float4 guide = g_textures[g_restir.previousGuides].Load(int3(previous, 0));
    if (!SameSurface(ViewDepth(frame, surface.position), surface.faceNormal, guide.w, guide.xyz)) {
        return;
    }
    Reservoir history = Unpack(g_reservoirs[g_restir.previousReservoirs][PixelIndex(previous)]);
    history.count = min(history.count, kMaxHistory);
    if (history.light != kNoLight) {
        history.light = history.light < g_restir.previousLightCount ? g_lightMaps[g_restir.lightMap][history.light] : kNoLight;
    }
    // A history that kept no light, because its light was hidden or is gone, is passed over: weighing its
    // confidence would darken the new sample. Unbiased, it stays: the same surface sees the same lights, and
    // its confidence is its share of the estimate.
    const bool unbiased = (g_restir.flags & kRestirUnbiased) != 0;
    if (history.light == kNoLight && !unbiased) {
        return;
    }

    const Receiver receiver = ReceiverOf(frame, surface);
    const Reservoir current = Unpack(g_reservoirs[g_restir.reservoirs][PixelIndex(pixel)]);
    uint random = Pcg(PixelIndex(pixel) * 7919u + g_restir.seed * 104729u + 2u);
    const float total = current.count + history.count;
    Reservoir combined = EmptyReservoir();
    // The new sample was tested for visibility already. A light the last frame did not have, the history
    // could not have found: the new sample takes its whole share, and a light that comes on shows at once.
    const float currentTarget = TargetAt(frame, receiver, current.light, current.u, false);
    const bool fresh =
        current.light != kNoLight &&
        (g_lightMaps[g_restir.lightMap][g_restir.previousLightCount + current.light] & kLightNew) != 0;
    Stream(combined, current.light, current.u, (fresh ? 1.0 : current.count / total) * currentTarget * current.weight,
           currentTarget, Random(random));
    const float historyTarget = TargetAt(frame, receiver, history.light, history.u, unbiased);
    Stream(combined, history.light, history.u, history.count / total * historyTarget * history.weight, historyTarget,
           Random(random));
    combined.count = total;
    combined.weight = combined.target > 0.0 ? combined.weightSum / combined.target : 0.0;
    if (combined.weight <= 0.0) {
        combined.light = kNoLight;
    }
    g_reservoirs[g_restir.reservoirs][PixelIndex(pixel)] = Pack(combined);
}
