// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

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
#include <filesystem>
#include <format>
#include <limits>
#include <string_view>

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";

// SDL has process-wide state, so only one Platform may exist at a time.
std::atomic<bool> s_platformExists{false};

// A gamepad sends events only while it is open. Returns false if it cannot be opened, in which
// case its connection is not reported.
bool OpenGamepad(detail::PlatformState& state, SDL_JoystickID id) {
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (gamepad == nullptr) {
        DAI_LOG_WARNING(kModule, "Cannot open gamepad {}: {}", id, SDL_GetError());
        return false;
    }
    state.gamepads.push_back(gamepad);
    const char* name = SDL_GetGamepadName(gamepad);
    DAI_LOG_INFO(kModule, "Gamepad {} connected: {}", id, (name != nullptr) ? name : "unnamed");
    return true;
}

// Returns false for a gamepad that was never opened, whose disconnection is not reported either.
bool CloseGamepad(detail::PlatformState& state, SDL_JoystickID id) {
    const auto found =
        std::ranges::find_if(state.gamepads, [id](SDL_Gamepad* gamepad) { return SDL_GetGamepadID(gamepad) == id; });
    if (found == state.gamepads.end()) {
        return false;
    }
    SDL_CloseGamepad(*found);
    state.gamepads.erase(found);
    DAI_LOG_INFO(kModule, "Gamepad {} disconnected", id);
    return true;
}

void AppendPendingEvents(detail::PlatformState& state, std::vector<Event>& events) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED && !OpenGamepad(state, event.gdevice.which)) {
            continue;
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED && !CloseGamepad(state, event.gdevice.which)) {
            continue;
        }
        if (std::optional<Event> translated = detail::TranslateEvent(event)) {
            events.push_back(*translated);
        }
    }
}

} // namespace

detail::PlatformState::~PlatformState() {
    DAI_ASSERT(liveWindows == 0, "Every Window must be destroyed before its Platform");
    for (SDL_Gamepad* gamepad : gamepads) {
        SDL_CloseGamepad(gamepad);
    }
    SDL_Quit();
    s_platformExists.store(false);
    DAI_LOG_DEBUG(kModule, "Platform shut down");
}

core::Result<Platform> Platform::Create(const PlatformConfig& config) {
    if (s_platformExists.exchange(true)) {
        return core::MakeError(core::ErrorCode::InvalidState, kModule, "A Platform already exists");
    }

    SDL_SetMainReady();
    if (config.videoDriver.empty()) {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    } else {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, config.videoDriver.c_str());
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, config.directInputGamepads ? "1" : "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::string message = std::format("Cannot initialize SDL video: {}", SDL_GetError());
        SDL_Quit();
        s_platformExists.store(false);
        return core::MakeError(core::ErrorCode::DeviceError, kModule, std::move(message));
    }

    auto state = std::make_unique<detail::PlatformState>();
    const char* driver = SDL_GetCurrentVideoDriver();
    state->videoDriver = (driver != nullptr) ? driver : "";

    // Gamepads are optional: without them the keyboard and mouse still work.
    state->gamepadsAvailable = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!state->gamepadsAvailable) {
        DAI_LOG_WARNING(kModule, "Gamepads are unavailable: {}", SDL_GetError());
    }

    const int version = SDL_GetVersion();
    DAI_LOG_INFO(kModule, "SDL {}.{}.{} initialized, video driver '{}'", SDL_VERSIONNUM_MAJOR(version),
                 SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), state->videoDriver);
    return Platform(std::move(state));
}

std::filesystem::path ExecutableDirectory() {
    const char* directory = SDL_GetBasePath(); // UTF-8, owned by SDL.
    if (directory == nullptr) {
        DAI_LOG_WARNING("platform", "Executable directory unknown: {}", SDL_GetError());
        return {};
    }
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(directory)));
}

std::string ExecutableFileName(std::string_view program) {
#ifdef _WIN32
    return std::string(program) + ".exe";
#else
    return std::string(program);
#endif
}

std::string KeyLabel(Key key) {
    // Key values are USB HID usages, which SDL's scancodes are.
    const SDL_Keycode code = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(key), SDL_KMOD_NONE, false);
    const char* name = code != SDLK_UNKNOWN ? SDL_GetKeyName(code) : nullptr;
    return name != nullptr ? std::string(name) : std::string();
}

void ShowErrorMessage(const std::string& title, const std::string& message) {
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), nullptr)) {
        DAI_LOG_WARNING("platform", "Cannot show an error box: {}", SDL_GetError());
    }
}

Platform::Platform(std::unique_ptr<detail::PlatformState> state) : m_state(std::move(state)) {}
Platform::Platform(Platform&& other) noexcept = default;
Platform& Platform::operator=(Platform&& other) noexcept = default;
Platform::~Platform() = default;

void Platform::PollEvents(std::vector<Event>& events) {
    AppendPendingEvents(*m_state, events);
}

void Platform::WaitEvents(std::chrono::milliseconds timeout, std::vector<Event>& events) {
    const auto milliseconds = static_cast<Sint32>(
        std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, std::numeric_limits<Sint32>::max()));
    // Waiting with a null event leaves the event in the queue for AppendPendingEvents.
    SDL_WaitEventTimeout(nullptr, milliseconds);
    AppendPendingEvents(*m_state, events);
}

bool Platform::GamepadsAvailable() const {
    return m_state->gamepadsAvailable;
}

void Platform::RumbleGamepads(float low, float high, std::chrono::milliseconds duration) {
    constexpr float kFull = std::numeric_limits<Uint16>::max();
    const auto strength = [](float value) { return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * kFull); };
    const auto milliseconds =
        static_cast<Uint32>(std::clamp<std::chrono::milliseconds::rep>(duration.count(), 0, 60000));
    for (SDL_Gamepad* gamepad : m_state->gamepads) {
        // A pad without motors says so; that is not worth a log line on every frame.
        SDL_RumbleGamepad(gamepad, strength(low), strength(high), milliseconds);
    }
}

std::string_view Platform::VideoDriver() const {
    return m_state->videoDriver;
}

} // namespace daidalos::platform
