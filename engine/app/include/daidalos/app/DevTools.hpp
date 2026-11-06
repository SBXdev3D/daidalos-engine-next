// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#if !DAI_DEV_TOOLS
#error "The developer tools exist in builds with DAI_DEV_TOOLS only"
#endif

#include <daidalos/app/Console.hpp>
#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/ecs/World.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace daidalos::app {

class Application;

/// The developer tools of development builds (ADR-0030), drawn with the Dear ImGui overlay: a console,
/// frame statistics, the entities of the current scene with their components, and the loaded
/// resources. The key under Escape (Grave) opens and closes them; while they are open, the game gets no
/// keyboard or mouse input and the pointer is free. Closed, a line of statistics stays in a corner.
class DevTools {
public:
    /// The application owns the tools and outlives them.
    explicit DevTools(Application& app);

    [[nodiscard]] bool IsOpen() const { return m_open; }
    /// Called by the application, which also frees the pointer and holds the game's input.
    void SetOpen(bool open);

    [[nodiscard]] Console& GetConsole() { return m_console; }

    /// Shows every window, the game's too, as the console's "windows" command does.
    void ShowAllWindows();
    /// Shows the entity in the inspector.
    void Select(ecs::Entity entity) { m_selected = entity; }
    [[nodiscard]] ecs::Entity Selected() const { return m_selected; }

    /// Shows components of type T in the entity inspector, under the name, with ImGui calls that may
    /// edit it. The engine's own components are already there.
    template <ecs::Component T>
    void AddInspector(std::string name, std::function<void(T&)> draw) {
        m_inspectors.push_back(
            {std::move(name), [draw = std::move(draw)](ecs::World& world, ecs::Entity entity, bool drawIt) {
                 T* component = world.TryGet<T>(entity);
                 if (component != nullptr && drawIt) {
                     draw(*component);
                 }
                 return component != nullptr;
             }});
    }

    /// A window of the game's own, listed in the tools' menu, drawn with ImGui calls while they are open.
    void AddWindow(std::string name, std::function<void()> draw);

    /// Draws the tools for this frame, between the overlay's BeginFrame and EndFrame.
    void Draw(const core::FrameTime& time);

private:
    struct Inspector {
        std::string name;
        /// Whether the entity has the component, and draws it too when asked.
        std::function<bool(ecs::World&, ecs::Entity, bool draw)> draw;
    };
    struct Window {
        std::string name;
        std::function<void()> draw;
        bool visible = true;
    };

    void AddEngineInspectors();
    void DrawMenu();
    void DrawSummary();
    void DrawConsole();
    void DrawStats();
    void DrawEntities();
    void DrawEntityTree(ecs::World& world, ecs::Entity entity);
    void DrawInspector(ecs::World& world);
    void DrawResources();

    Application* m_app;
    Console m_console;
    bool m_open = false;
    bool m_showConsole = true;
    bool m_showStats = true;
    bool m_showEntities = false;
    bool m_showResources = false;
    std::vector<Inspector> m_inspectors;
    std::vector<Window> m_windows;

    // Console.
    std::array<char, 512> m_input{};
    /// Place in the history while browsing it with the arrows; the history's size when not.
    std::size_t m_historyPlace = 0;
    bool m_focusInput = false;
    bool m_scrollToBottom = true;
    std::uint64_t m_logRevision = static_cast<std::uint64_t>(-1);
    std::vector<ConsoleLog::Line> m_lines;

    // Statistics: the last frames' durations, in milliseconds, oldest first from m_frameCursor.
    static constexpr std::size_t kFrameHistory = 240;
    std::array<float, kFrameHistory> m_frameTimes{};
    std::size_t m_frameCursor = 0;
    std::size_t m_frameCount = 0;

    // Entities.
    std::array<char, 64> m_filter{};
    ecs::Entity m_selected;
};

} // namespace daidalos::app
