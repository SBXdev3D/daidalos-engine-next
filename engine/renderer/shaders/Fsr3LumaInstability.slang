// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// FSR 3.1 (ADR-0050): how unstable each pixel's luma has been over the last frames.

#include "Fsr3.slangh"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"
#include "fsr3upscaler/ffx_fsr3upscaler_luma_instability.h"

[numthreads(8, 8, 1)]
void CSMain(int2 pixel: SV_DispatchThreadID) {
    LumaInstability(pixel);
}
