// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/platform/Window.hpp>

#include <SDL3/SDL.h>
#include <vulkan/vulkan_core.h>
// SDL declares its own Vulkan types unless vulkan_core.h comes first.
#include <SDL3/SDL_vulkan.h>

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

    constexpr auto kMaxSize = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (config.width == 0 || config.height == 0 || config.width > kMaxSize || config.height > kMaxSize) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("Invalid window size {}x{}", config.width, config.height));
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }
    if (config.fullscreen) {
        // Without a display mode, SDL covers the display at its desktop resolution.
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window* window =
        SDL_CreateWindow(config.title.c_str(), static_cast<int>(config.width), static_cast<int>(config.height), flags);
    if (window == nullptr) {
        return core::MakeError(
            core::ErrorCode::DeviceError, kModule,
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
    return static_cast<WindowId>(SDL_GetWindowID(m_impl->window));
}

Extent Window::Size() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_impl->window, &width, &height);
    return ToExtent(width, height);
}

Extent Window::PixelSize() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->window, &width, &height);
    return ToExtent(width, height);
}

bool Window::IsMinimized() const {
    return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_MINIMIZED) != 0;
}

std::string Window::Title() const {
    return SDL_GetWindowTitle(m_impl->window);
}

void Window::SetMouseCaptured(bool captured) {
    SDL_SetWindowRelativeMouseMode(m_impl->window, captured);
}

bool Window::IsMouseCaptured() const {
    return SDL_GetWindowRelativeMouseMode(m_impl->window);
}

void Window::SetTextInput(bool enabled) {
    if (enabled) {
        SDL_StartTextInput(m_impl->window);
    } else {
        SDL_StopTextInput(m_impl->window);
    }
}

bool Window::IsTextInputEnabled() const {
    return SDL_TextInputActive(m_impl->window);
}

void Window::SetTitle(std::string_view title) {
    SDL_SetWindowTitle(m_impl->window, std::string(title).c_str());
}

bool Window::IsFullscreen() const {
    return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::SetFullscreen(bool fullscreen) {
    if (IsFullscreen() == fullscreen) {
        return;
    }
    if (!SDL_SetWindowFullscreen(m_impl->window, fullscreen)) {
        DAI_LOG_WARNING(kModule, "Cannot change to {}: {}", fullscreen ? "fullscreen" : "a window", SDL_GetError());
        return;
    }
    SDL_SyncWindow(m_impl->window);
}

void Window::SetSize(Extent size) {
    SDL_SetWindowSize(m_impl->window, static_cast<int>(size.width), static_cast<int>(size.height));
    SDL_SyncWindow(m_impl->window);
}

core::Result<std::vector<std::string>> Window::VulkanInstanceExtensions() const {
    DAI_ASSERT((SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_VULKAN) != 0,
               "The window was not created with WindowConfig::vulkan");

    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr) {
        return core::MakeError(core::ErrorCode::NotSupported, kModule,
                               std::format("Cannot list the Vulkan surface extensions: {}", SDL_GetError()));
    }
    return std::vector<std::string>(extensions, extensions + count);
}

core::Result<std::uint64_t> Window::CreateVulkanSurface(std::uintptr_t instance) const {
    DAI_ASSERT((SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_VULKAN) != 0,
               "The window was not created with WindowConfig::vulkan");

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(m_impl->window, reinterpret_cast<VkInstance>(instance), nullptr, &surface)) {
        return core::MakeError(core::ErrorCode::DeviceError, kModule,
                               std::format("Cannot create a Vulkan surface: {}", SDL_GetError()));
    }
    return reinterpret_cast<std::uint64_t>(surface);
}

} // namespace daidalos::platform
