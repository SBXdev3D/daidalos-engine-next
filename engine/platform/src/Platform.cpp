// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/Platform.hpp>

#include "EventTranslation.hpp"
#include "PlatformState.hpp"

#define SDL_MAIN_HANDLED // The engine does not use SDL's main() replacement.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <limits>

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";

// SDL has process-wide state, so only one Platform may exist at a time.
std::atomic<bool> s_platformExists{false};

void AppendPendingEvents(std::vector<Event>& events) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (std::optional<Event> translated = detail::TranslateEvent(event)) {
            events.push_back(*translated);
        }
    }
}

} // namespace

detail::PlatformState::~PlatformState() {
    DAI_ASSERT(liveWindows == 0, "Every Window must be destroyed before its Platform");
    SDL_Quit();
    s_platformExists.store(false);
    DAI_LOG_DEBUG(kModule, "Platform shut down");
}

core::Result<Platform> Platform::Create(const PlatformConfig& config) {
    if (s_platformExists.exchange(true)) {
        return core::MakeError(core::ErrorCode::InvalidState, std::string(kModule), "A Platform already exists");
    }

    SDL_SetMainReady();
    if (config.videoDriver.empty()) {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    } else {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, config.videoDriver.c_str());
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::string message = std::format("Cannot initialize SDL video: {}", SDL_GetError());
        SDL_Quit();
        s_platformExists.store(false);
        return core::MakeError(core::ErrorCode::DeviceError, std::string(kModule), std::move(message));
    }

    auto state = std::make_unique<detail::PlatformState>();
    const char* driver = SDL_GetCurrentVideoDriver();
    state->videoDriver = (driver != nullptr) ? driver : "";

    const int version = SDL_GetVersion();
    DAI_LOG_INFO(kModule, "SDL {}.{}.{} initialized, video driver '{}'", SDL_VERSIONNUM_MAJOR(version),
                 SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), state->videoDriver);
    return Platform(std::move(state));
}

Platform::Platform(std::unique_ptr<detail::PlatformState> state) : m_state(std::move(state)) {}
Platform::Platform(Platform&& other) noexcept = default;
Platform& Platform::operator=(Platform&& other) noexcept = default;
Platform::~Platform() = default;

void Platform::PollEvents(std::vector<Event>& events) {
    DAI_ASSERT(m_state, "Platform used after being moved from");
    AppendPendingEvents(events);
}

void Platform::WaitEvents(std::chrono::milliseconds timeout, std::vector<Event>& events) {
    DAI_ASSERT(m_state, "Platform used after being moved from");
    const auto milliseconds = static_cast<Sint32>(
        std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, std::numeric_limits<Sint32>::max()));
    // Waiting with a null event leaves the event in the queue for AppendPendingEvents.
    SDL_WaitEventTimeout(nullptr, milliseconds);
    AppendPendingEvents(events);
}

std::string_view Platform::VideoDriver() const {
    DAI_ASSERT(m_state, "Platform used after being moved from");
    return m_state->videoDriver;
}

} // namespace daidalos::platform
