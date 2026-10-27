// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Event.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace daidalos::platform {

class Platform;

struct WindowConfig {
    std::string title = "Daidalos Engine Next";
    /// Size in screen coordinates. The size in pixels can be larger on high-density displays.
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
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
    [[nodiscard]] std::string Title() const;

    void SetTitle(std::string_view title);

private:
    struct Impl;

    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::platform
