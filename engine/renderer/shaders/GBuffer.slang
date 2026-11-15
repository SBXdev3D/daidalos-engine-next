// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The G-buffer (ADR-0044): the one raster pass over the opaque meshes. It writes their depth, their world
// normals and roughness, their motion vectors (ADR-0037), and what lighting needs of their material: the
// base color and metalness, the emission and the occlusion. Every texture of the material is sampled here,
// once per pixel; the lighting pass and the composite read the result. Glass is left out: the glass pass
// draws it over the finished frame (ADR-0042).
// Its vertex shader, VSMain in MeshCommon.slangh, is the glass pass's too, so that both compute the same
// depth.

#include "MeshCommon.slangh"
#include "GBufferSurface.slangh"
