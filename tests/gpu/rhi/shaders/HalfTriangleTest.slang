// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// One triangle over the lower-left half of two targets, below their diagonal: white in the first, a
// quarter of red in the second.

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    const float2 corners[3] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0)};
    return float4(corners[vertexId], 1.0, 1.0);
}

struct Targets {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

Targets PSMain() {
    Targets targets;
    targets.first = float4(1.0, 1.0, 1.0, 1.0);
    targets.second = float4(0.25, 0.0, 0.0, 1.0);
    return targets;
}
