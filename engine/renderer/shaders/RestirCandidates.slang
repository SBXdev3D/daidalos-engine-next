// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// ReSTIR DI's first pass (ADR-0044): each pixel draws a few lights by power, keeps one in proportion to
// what it would send (DrawCandidates), and tests it with a shadow ray before anything reuses it: indoors,
// most lights are behind a wall, and an occluded sample would spread to the neighbours. It also writes the
// pixel's face normal and depth, which the spatial reuse reads its neighbours' from, and the next frame's
// temporal reuse compares its own with.

#include "Restir.slangh"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(g_restir.width, g_restir.height))) {
        return;
    }
    const FrameData frame = g_frames[g_restir.frameBuffer][0];
    const int2 pixel = int2(id.xy);
    const GBufferSurface surface = ReadGBuffer(frame, Textures(), pixel);
    if (!surface.found) {
        g_reservoirs[g_restir.reservoirs][PixelIndex(pixel)] = Pack(EmptyReservoir());
        g_storageImages[g_restir.guides][id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const Receiver receiver = ReceiverOf(frame, surface);
    uint random = Pcg(PixelIndex(pixel) * 9781u + g_restir.seed * 26699u + 1u);
    Reservoir reservoir = DrawCandidates(frame, receiver, random);
    if (reservoir.light != kNoLight &&
        !Unoccluded(frame, receiver, ContactLight(frame, reservoir.light, reservoir.u, receiver),
                    g_restir.instanceBuffer, g_restir.materialBuffer)) {
        reservoir.light = kNoLight;
        reservoir.weight = 0.0;
    }
    g_reservoirs[g_restir.reservoirs][PixelIndex(pixel)] = Pack(reservoir);
    g_storageImages[g_restir.guides][id.xy] = float4(surface.faceNormal, ViewDepth(frame, surface.position));
}
