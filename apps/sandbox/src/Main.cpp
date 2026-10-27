// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>

#include <cstdio>
#include <format>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace daidalos;

// Opens a window and shows the frame rate in its title until the window is closed.
class Sandbox final : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(app::Application& app) override {
        DAI_LOG_INFO("sandbox", "Daidalos Engine Next {} started", DAI_VERSION);
        const platform::Extent size = app.MainWindow().PixelSize();
        DAI_LOG_INFO("sandbox", "Main window: {}x{} pixels", size.width, size.height);
        return {};
    }

    void OnEvent(app::Application& /*app*/, const platform::Event& event) override {
        if (const auto* resized = std::get_if<platform::WindowResized>(&event)) {
            DAI_LOG_DEBUG("sandbox", "Resized to {}x{} pixels", resized->pixelSize.width, resized->pixelSize.height);
        }
    }

    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        ++m_frames;
        m_elapsed += time.unclampedDelta;
        if (m_elapsed >= core::Seconds{1.0}) {
            const double fps = static_cast<double>(m_frames) / m_elapsed.count();
            app.MainWindow().SetTitle(std::format("Daidalos Sandbox - {:.0f} FPS", fps));
            m_frames = 0;
            m_elapsed = core::Seconds::zero();
        }
    }

    void OnStop(app::Application& /*app*/) override { DAI_LOG_INFO("sandbox", "Stopping"); }

private:
    int m_frames = 0;
    core::Seconds m_elapsed{};
};

} // namespace

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.minimumLevel = logging::Level::Debug;
    config.log.filePath = "sandbox.log";
    config.crashDumpDirectory = "crashdumps";
    config.configFile = "sandbox.cfg";
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos Sandbox";

    app::Application application(std::move(config));
    Sandbox sandbox;
    if (const core::Result<void> result = application.Run(sandbox); !result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }
    return 0;
}
