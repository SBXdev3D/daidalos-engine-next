// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// FSR 3.1 (ADR-0050): the luma's and the farthest depth's pyramid, and the frame's average luma.

#include "Fsr3.slangh"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"
#include "fsr3upscaler/ffx_fsr3upscaler_luma_pyramid.h"

[numthreads(256, 1, 1)]
void CSMain(uint3 group: SV_GroupID, uint thread: SV_GroupIndex) {
    ComputeAutoExposure(group, thread);
}
