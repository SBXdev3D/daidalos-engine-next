// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Event.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>

namespace daidalos::debugui {

/// Dear ImGui overlay (ADR-0015), drawn through the RHI and fed with platform events, so it depends on
/// neither SDL nor Vulkan. Between BeginFrame and EndFrame, code calls ImGui functions directly.
/// Text fields need platform::Window::SetTextInput to be on.
///
/// Each frame: ProcessEvent() for every event, BeginFrame(), ImGui calls, EndFrame(), then Draw()
/// inside a rendering scope if the frame is drawn. Only one DebugUi can exist at a time.
class DebugUi {
public:
    /// The Device must outlive the DebugUi.
    static core::Result<DebugUi> Create(rhi::Device& device, rhi::Format colorFormat);

    DebugUi(DebugUi&& other) noexcept;
    DebugUi& operator=(DebugUi&& other) noexcept;
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;
    ~DebugUi();

    void ProcessEvent(const platform::Event& event);

    /// The window size is in screen coordinates, as mouse positions are.
    void BeginFrame(std::chrono::duration<double> delta, platform::Extent windowSize, platform::Extent pixelSize);
    void EndFrame();

    /// Records the frame built by EndFrame. An error means that a vertex buffer could not grow.
    [[nodiscard]] core::Result<void> Draw(rhi::CommandList& commands, rhi::Extent extent);

    /// True when the mouse is over the UI: game code should ignore it.
    [[nodiscard]] bool WantsMouse() const;
    /// True when the UI has the keyboard focus.
    [[nodiscard]] bool WantsKeyboard() const;

private:
    struct Impl;

    explicit DebugUi(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::debugui
