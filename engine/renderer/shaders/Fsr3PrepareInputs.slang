// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// FSR 3.1's first pass (ADR-0050): dilates the depth and motion vectors, reprojects the nearest depth
// into the previous frame and computes the frame's luma.

#include "Fsr3.slangh"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"
#include "fsr3upscaler/ffx_fsr3upscaler_prepare_inputs.h"

[numthreads(8, 8, 1)]
void CSMain(int2 pixel: SV_DispatchThreadID) {
    PrepareInputs(pixel);
}
