// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

// Profiling macros (ADR-0015). They expand to Tracy calls when the engine is built with
// DAI_ENABLE_PROFILER (the *-profile presets), and to nothing otherwise. The client runs on demand:
// it records nothing until the Tracy profiler connects.
//
//   void Renderer::Render() {
//       DAI_PROFILE_FUNCTION();
//       { DAI_PROFILE_SCOPE("Culling"); ... }
//   }
//
// DAI_PROFILE_SCOPE takes a string literal. DAI_PROFILE_SCOPE_TEXT takes a name known only at run
// time (a std::string or std::string_view, copied by Tracy); it costs a little more.

#if defined(DAI_PROFILER_ENABLED)

#include <tracy/Tracy.hpp>

/// Marks the end of a frame. Called once per frame by the application loop.
#define DAI_PROFILE_FRAME() FrameMark
/// Times the enclosing scope, named after the function.
#define DAI_PROFILE_FUNCTION() ZoneScoped
/// Times the enclosing scope under the given name.
#define DAI_PROFILE_SCOPE(name) ZoneScopedN(name)
/// Times the enclosing scope under a name built at run time.
#define DAI_PROFILE_SCOPE_TEXT(text)                                                                                   \
    ZoneScoped;                                                                                                        \
    ZoneName((text).data(), (text).size())

#else

#define DAI_PROFILE_FRAME() static_cast<void>(0)
#define DAI_PROFILE_FUNCTION() static_cast<void>(0)
#define DAI_PROFILE_SCOPE(name) static_cast<void>(0)
#define DAI_PROFILE_SCOPE_TEXT(text) static_cast<void>(sizeof(text))

#endif
