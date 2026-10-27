// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <string>

namespace daidalos::platform::detail {

/// State behind a Platform. Windows keep a pointer to it, which stays valid when the Platform is moved.
struct PlatformState {
    PlatformState() = default;
    PlatformState(const PlatformState&) = delete;
    PlatformState& operator=(const PlatformState&) = delete;
    ~PlatformState();

    std::string videoDriver;
    int liveWindows = 0;
};

} // namespace daidalos::platform::detail
