// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// FSR 3.1 (ADR-0050): the reactive, disocclusion and shading change masks, and the new locks.

#include "Fsr3.slangh"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"
#include "fsr3upscaler/ffx_fsr3upscaler_prepare_reactivity.h"

[numthreads(8, 8, 1)]
void CSMain(int2 pixel: SV_DispatchThreadID) {
    PrepareReactivity(pixel);
}
