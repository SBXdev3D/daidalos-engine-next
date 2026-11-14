// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Event.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::platform {

class Platform;

struct WindowConfig {
    std::string title = "Daidalos Engine Next";
    /// Size in screen coordinates. The size in pixels can be larger on high-density displays.
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    /// Covers its display, without borders, at the display's own resolution.
    bool fullscreen = false;
    /// Prepares the window for a Vulkan surface. Creation fails on video drivers without Vulkan
    /// support, such as "dummy".
    bool vulkan = false;
};

/// The high dynamic range of the display a window is on, as the system's settings describe it.
struct HdrInfo {
    /// The display shows HDR content; false on SDR displays and where the system does not say.
    bool enabled = false;
    /// The brightness of SDR white, in nits: what the system shows a white page at.
    float sdrWhiteNits = 80.0f;
    /// How many times brighter than SDR white the display can go: 1 without HDR.
    float headroom = 1.0f;
};

/// A desktop window. It must be destroyed before the Platform that created it.
class Window {
public:
    /// Fails with InvalidArgument for a zero or oversized size, or DeviceError if the window cannot be created.
    static core::Result<Window> Create(Platform& platform, const WindowConfig& config);

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    [[nodiscard]] WindowId Id() const;
    /// Size in screen coordinates.
    [[nodiscard]] Extent Size() const;
    /// Size of the drawable area in pixels.
    [[nodiscard]] Extent PixelSize() const;
    [[nodiscard]] bool IsMinimized() const;
    /// Read from the system each time: it changes when the window moves to another display, or when
    /// HDR is turned on or off.
    [[nodiscard]] HdrInfo Hdr() const;
    [[nodiscard]] bool IsFullscreen() const;
    [[nodiscard]] std::string Title() const;

    void SetTitle(std::string_view title);
    /// Hides the pointer and reports motion without limits, for looking around in a game.
    void SetMouseCaptured(bool captured);
    [[nodiscard]] bool IsMouseCaptured() const;
    /// Delivers TextEntered events while on, for text fields. Off by default, because some systems show
    /// an on-screen keyboard or an input method window while it is on.
    void SetTextInput(bool enabled);
    [[nodiscard]] bool IsTextInputEnabled() const;
    /// Resizes the window, in screen coordinates, and waits for the system to apply it.
    void SetSize(Extent size);
    /// Covers the display without borders, or goes back to a window, and waits for the system to apply
    /// it. Does nothing if the window is already so.
    void SetFullscreen(bool fullscreen);

    /// Instance extensions that a Vulkan surface for this window needs.
    /// The window must have been created with WindowConfig::vulkan.
    [[nodiscard]] core::Result<std::vector<std::string>> VulkanInstanceExtensions() const;
    /// Creates a VkSurfaceKHR for this window. The instance is a VkInstance and the result a
    /// VkSurfaceKHR, carried as integers to keep Vulkan types out of this header. The caller owns
    /// the surface, which must be destroyed before the window.
    [[nodiscard]] core::Result<std::uint64_t> CreateVulkanSurface(std::uintptr_t instance) const;

private:
    struct Impl;

    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::platform
