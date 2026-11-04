// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// One white triangle over the lower-left half of the target, below its diagonal. Writes depth 1.

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position {
    const float2 corners[3] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0)};
    return float4(corners[vertexId], 1.0, 1.0);
}

float4 PSMain() : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}
