// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/debugui/DebugUi.hpp>

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>

#include "GpuFixture.hpp"

using namespace daidalos;
using gpu_tests::GpuTest;

namespace {

constexpr rhi::Extent kExtent{256, 256};
constexpr platform::Extent kWindow{256, 256};
constexpr rhi::Format kFormat = rhi::Format::Rgba8Srgb; // As on the swapchain.
constexpr std::chrono::duration<double> kDelta{1.0 / 60.0};

class DebugUiTest : public GpuTest {
protected:
    void SetUp() override {
        GpuTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<debugui::DebugUi> ui = debugui::DebugUi::Create(Device(), kFormat);
        ASSERT_TRUE(ui.has_value()) << core::Format(ui.error());
        m_ui.emplace(std::move(*ui));
    }

    void TearDown() override {
        m_ui.reset();
        GpuTest::TearDown();
    }

    std::optional<debugui::DebugUi> m_ui;
};

} // namespace

TEST_F(DebugUiTest, DrawsAWindowOnTheTarget) {
    const core::Result<rhi::TextureHandle> color =
        Device().CreateTexture({.extent = kExtent,
                                .format = kFormat,
                                .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                .name = "Color"});
    ASSERT_TRUE(color.has_value());
    const std::uint64_t size = std::uint64_t{kExtent.width} * kExtent.height * 4;
    const core::Result<rhi::BufferHandle> readback = Device().CreateBuffer({.size = size,
                                                                            .usage = rhi::BufferUsage::TransferDst,
                                                                            .memory = rhi::MemoryLocation::GpuToCpu,
                                                                            .name = "Readback"});
    ASSERT_TRUE(readback.has_value());

    m_ui->BeginFrame(kDelta, kWindow, kWindow);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(128.0f, 128.0f));
    ImGui::Begin("Test");
    ImGui::Text("Hello");
    ImGui::End();
    m_ui->EndFrame();

    core::Result<void> drawn;
    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Discard(*color, rhi::TextureState::ColorTarget);
                        commands.BeginRendering(
                            {.colorTarget = *color, .clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)});
                        drawn = m_ui->Draw(commands, kExtent);
                        commands.EndRendering();
                        commands.Transition(*color, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(*color, *readback);
                    })
                    .has_value());
    ASSERT_TRUE(drawn.has_value());

    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        std::array<std::uint8_t, 4> value{};
        std::memcpy(value.data(), Device().Mapped(*readback).data() + (std::size_t{y} * kExtent.width + x) * 4, 4);
        return value[0] + value[1] + value[2];
    };
    EXPECT_GT(pixel(64, 64), 0);   // Inside the window, which is at the top left.
    EXPECT_EQ(pixel(200, 200), 0); // Outside.

    Device().DestroyTexture(*color);
    Device().DestroyBuffer(*readback);
}

TEST_F(DebugUiTest, KeysAndButtonsReachImGui) {
    m_ui->ProcessEvent(platform::KeyPressed{{}, platform::Key::Space, false});
    m_ui->ProcessEvent(platform::KeyPressed{{}, platform::Key::Z, false});
    m_ui->ProcessEvent(platform::MouseMoved{{}, glm::vec2(10.0f, 20.0f), glm::vec2(0.0f)});
    m_ui->ProcessEvent(platform::MouseButtonPressed{{}, platform::MouseButton::Right, glm::vec2(10.0f, 20.0f)});

    // ImGui spreads queued events over several frames, so that short presses are not lost.
    for (int frame = 0; frame < 4; ++frame) {
        m_ui->BeginFrame(kDelta, kWindow, kWindow);
        m_ui->EndFrame();
    }

    m_ui->BeginFrame(kDelta, kWindow, kWindow);
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_Space));
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_Z));
    EXPECT_TRUE(ImGui::IsMouseDown(ImGuiMouseButton_Right));
    EXPECT_EQ(ImGui::GetMousePos().x, 10.0f);
    m_ui->EndFrame();
}

TEST_F(DebugUiTest, MouseOverAWindowIsCaptured) {
    m_ui->ProcessEvent(platform::MouseMoved{{}, glm::vec2(10.0f, 10.0f), glm::vec2(0.0f)});
    for (int frame = 0; frame < 2; ++frame) {
        m_ui->BeginFrame(kDelta, kWindow, kWindow);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(128.0f, 128.0f));
        ImGui::Begin("Test");
        ImGui::End();
        m_ui->EndFrame();
    }
    EXPECT_TRUE(m_ui->WantsMouse());
}
