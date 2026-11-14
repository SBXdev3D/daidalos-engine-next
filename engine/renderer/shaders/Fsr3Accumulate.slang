// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// FSR 3.1 (ADR-0050): upsamples the frame to the output and blends it with the reprojected history.

#include "Fsr3.slangh"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"
#include "fsr3upscaler/ffx_fsr3upscaler_sample.h"
#include "fsr3upscaler/ffx_fsr3upscaler_upsample.h"
#include "fsr3upscaler/ffx_fsr3upscaler_reproject.h"
#include "fsr3upscaler/ffx_fsr3upscaler_accumulate.h"

[numthreads(8, 8, 1)]
void CSMain(int2 pixel: SV_DispatchThreadID) {
    Accumulate(pixel);
}
