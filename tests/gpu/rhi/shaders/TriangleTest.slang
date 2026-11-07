// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// One white triangle over the whole target.

float4 VSMain(uint vertexId : SV_VulkanVertexID) : SV_Position {
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain() : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}
