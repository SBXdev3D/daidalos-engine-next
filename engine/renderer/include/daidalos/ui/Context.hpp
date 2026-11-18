// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/input/InputState.hpp>
#include <daidalos/ui/Canvas.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace daidalos::ui {

/// How widgets look. Colors are sRGB with straight alpha, as on the canvas.
struct WidgetStyle {
    const Font* font = nullptr;
    float textSize = 34.0f;
    glm::vec4 text{0.78f, 0.78f, 0.78f, 1.0f};
    glm::vec4 focusedText{1.0f};
    glm::vec4 background{0.0f, 0.0f, 0.0f, 0.55f};
    glm::vec4 focusedBackground{0.55f, 0.07f, 0.06f, 0.9f};
    /// A slider's track, and the part of it up to the value.
    glm::vec4 track{1.0f, 1.0f, 1.0f, 0.2f};
    glm::vec4 accent{0.85f, 0.15f, 0.1f, 1.0f};
    /// Canvas units between the edges of a widget and its text.
    float padding = 24.0f;
    /// What a toggle shows on and off, in the game's language.
    std::string onText = "ON";
    std::string offText = "OFF";
};

/// Buttons, toggles, sliders and choices, immediate mode like the canvas they draw on (ADR-0029): each
/// frame, the game calls Begin, then one function per widget, which draws it and says whether the player
/// used it. The game keeps the values; the context only remembers which widget has the focus, by its
/// place in the frame's calls, so a screen must draw the same widgets in the same order every frame.
///
/// Mouse: the widget under the pointer takes the focus when the pointer moves, and a left click uses it.
/// Keyboard: up and down move the focus, wrapping around; Enter or Space use the focused widget; left
/// and right change its value. Gamepad: the D-pad or the left stick as the arrows, South as Enter.
class Context {
public:
    /// Starts this frame's widgets, on a canvas begun for the frame. windowSize is in the units of
    /// mouse positions (platform::Window::Size), which places the pointer on the canvas.
    void Begin(Canvas& canvas, const input::InputState& input, glm::vec2 windowSize);

    /// Gives the focus to the first widget, for a new screen, and ignores the input for the rest of the
    /// frame, so that the click or key that changed screens does not also use a widget of the new one.
    void ResetFocus();

    void SetStyle(const WidgetStyle& style) { m_style = style; }
    /// While held, from the next Begin, the widgets read no input and the focus stays: a menu waiting for the
    /// key to bind to an action reads the input itself.
    void Hold(bool held) { m_held = held; }
    [[nodiscard]] const WidgetStyle& Style() const { return m_style; }

    /// The pointer, in canvas units.
    [[nodiscard]] glm::vec2 Pointer() const { return m_pointer; }

    /// The label in the middle. True when used.
    bool Button(std::string_view label, const Rect& rect);
    /// The label on the left, ON or OFF on the right. True when the value changed.
    bool Toggle(std::string_view label, const Rect& rect, bool& value);
    /// The label on the left, then a track the pointer can drag, then valueText, which the game formats
    /// from the value. Values are min plus whole steps, up to max. True when the value changed.
    bool Slider(std::string_view label, const Rect& rect, float& value, float min, float max, float step,
                std::string_view valueText);
    /// The label on the left and the chosen option on the right, between arrows. Using it, or right,
    /// goes to the next option; left, or a click on the left half of the option, to the previous one.
    /// Both wrap around. True when the index changed.
    bool Choice(std::string_view label, const Rect& rect, std::size_t& index,
                std::span<const std::string_view> options);

private:
    struct Widget {
        bool focused = false;
        bool hovered = false;
        /// Clicked, or Enter or Space while focused.
        bool used = false;
        /// Pressed while focused, or 0.
        int step = 0;
    };

    /// Takes the next place in the frame, and reads the input for a widget there.
    Widget Next(const Rect& rect);
    void DrawBackground(const Rect& rect, bool focused);
    void DrawLabel(std::string_view text, const Rect& rect, bool focused, Align horizontal);

    Canvas* m_canvas = nullptr;
    WidgetStyle m_style;
    glm::vec2 m_pointer{0.0f};
    bool m_pointerMoved = false;
    bool m_click = false;
    bool m_mouseDown = false;
    bool m_activate = false;
    int m_step = 0;
    /// Where the left sticks leaned last frame, so that a lean moves once.
    int m_leanX = 0;
    int m_leanY = 0;
    /// Input is ignored for the rest of the frame.
    bool m_consumed = false;
    bool m_held = false;
    /// Places in the frame's calls.
    std::size_t m_focus = 0;
    std::size_t m_next = 0;
    /// The slider being dragged, or none.
    std::size_t m_dragging = kNone;

    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
};

} // namespace daidalos::ui
