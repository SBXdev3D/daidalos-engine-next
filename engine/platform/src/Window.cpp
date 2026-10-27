// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/platform/Window.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <limits>

#include "PlatformState.hpp"

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";

Extent ToExtent(int width, int height) {
    return Extent{static_cast<std::uint32_t>(std::max(width, 0)), static_cast<std::uint32_t>(std::max(height, 0))};
}

} // namespace

struct Window::Impl {
    Impl(SDL_Window* sdlWindow, detail::PlatformState& platformState) : window(sdlWindow), platform(&platformState) {
        ++platform->liveWindows;
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        SDL_DestroyWindow(window);
        --platform->liveWindows;
    }

    SDL_Window* window;
    detail::PlatformState* platform;
};

core::Result<Window> Window::Create(Platform& platform, const WindowConfig& config) {
    DAI_ASSERT(platform.m_state, "Platform used after being moved from");

    constexpr auto kMaxSize = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (config.width == 0 || config.height == 0 || config.width > kMaxSize || config.height > kMaxSize) {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule),
                               std::format("Invalid window size {}x{}", config.width, config.height));
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* window =
        SDL_CreateWindow(config.title.c_str(), static_cast<int>(config.width), static_cast<int>(config.height), flags);
    if (window == nullptr) {
        return core::MakeError(
            core::ErrorCode::DeviceError, std::string(kModule),
            std::format("Cannot create a {}x{} window: {}", config.width, config.height, SDL_GetError()));
    }

    DAI_LOG_DEBUG(kModule, "Window {} created ({}x{})", SDL_GetWindowID(window), config.width, config.height);
    return Window(std::make_unique<Impl>(window, *platform.m_state));
}

Window::Window(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;
Window::~Window() = default;

WindowId Window::Id() const {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    return static_cast<WindowId>(SDL_GetWindowID(m_impl->window));
}

Extent Window::Size() const {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_impl->window, &width, &height);
    return ToExtent(width, height);
}

Extent Window::PixelSize() const {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->window, &width, &height);
    return ToExtent(width, height);
}

bool Window::IsMinimized() const {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_MINIMIZED) != 0;
}

std::string Window::Title() const {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    return SDL_GetWindowTitle(m_impl->window);
}

void Window::SetTitle(std::string_view title) {
    DAI_ASSERT(m_impl, "Window used after being moved from");
    SDL_SetWindowTitle(m_impl->window, std::string(title).c_str());
}

} // namespace daidalos::platform
