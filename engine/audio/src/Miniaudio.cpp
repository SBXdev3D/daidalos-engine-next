// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// miniaudio is a single header; this file holds its implementation. It decodes WAV by itself, and OGG
// Vorbis through stb_vorbis, whose declarations it must see first.

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
