// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/platform/Event.hpp>

#include <SDL3/SDL_events.h>

#include <optional>

namespace daidalos::platform::detail {

/// Converts an SDL event to an engine event. Returns nothing for events the engine does not use.
std::optional<Event> TranslateEvent(const SDL_Event& event);

} // namespace daidalos::platform::detail
