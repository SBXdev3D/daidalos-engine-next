// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/ui/Context.hpp>

#include <algorithm>
#include <cmath>

namespace daidalos::ui {

namespace {

using Key = platform::Key;

bool Contains(const Rect& rect, glm::vec2 point) {
    return point.x >= rect.position.x && point.y >= rect.position.y && point.x < rect.position.x + rect.size.x &&
           point.y < rect.position.y + rect.size.y;
}

/// The part of a row right of its label, where values go.
Rect ValueArea(const Rect& rect) {
    const float left = rect.size.x * 0.5f;
    return {{rect.position.x + left, rect.position.y}, {rect.size.x - left, rect.size.y}};
}

} // namespace

void Context::Begin(Canvas& canvas, const input::InputState& input, glm::vec2 windowSize) {
    m_canvas = &canvas;
    const std::size_t count = m_next;
    m_next = 0;
    m_consumed = false;

    const glm::vec2 scale = (windowSize.x > 0.0f && windowSize.y > 0.0f) ? canvas.Size() / windowSize : glm::vec2(0.0f);
    m_pointer = input.MousePosition() * scale;
    m_pointerMoved = input.MouseDelta() != glm::vec2(0.0f);
    m_click = input.WasPressed(platform::MouseButton::Left);
    m_mouseDown = input.IsDown(platform::MouseButton::Left);
    if (!m_mouseDown) {
        m_dragging = kNone;
    }

    m_activate = input.WasPressed(Key::Enter) || input.WasPressed(Key::NumpadEnter) || input.WasPressed(Key::Space);
    m_step = (input.WasPressed(Key::Right) ? 1 : 0) - (input.WasPressed(Key::Left) ? 1 : 0);
    if (count > 0) {
        if (input.WasPressed(Key::Down)) {
            m_focus = (m_focus + 1) % count;
        }
        if (input.WasPressed(Key::Up)) {
            m_focus = (m_focus + count - 1) % count;
        }
        m_focus = std::min(m_focus, count - 1);
    }
}

void Context::ResetFocus() {
    m_focus = 0;
    m_dragging = kNone;
    m_consumed = true;
}

Context::Widget Context::Next(const Rect& rect) {
    DAI_ASSERT(m_canvas != nullptr, "ui::Context::Begin comes before the widgets");
    const std::size_t place = m_next++;
    Widget widget;
    widget.hovered = !m_consumed && Contains(rect, m_pointer);
    if (widget.hovered && (m_pointerMoved || m_click)) {
        m_focus = place;
    }
    widget.focused = m_focus == place;
    if (!m_consumed) {
        widget.used = (widget.hovered && m_click) || (widget.focused && m_activate);
        widget.step = widget.focused ? m_step : 0;
    }
    return widget;
}

void Context::DrawBackground(const Rect& rect, bool focused) {
    m_canvas->FillRect(rect, focused ? m_style.focusedBackground : m_style.background);
}

void Context::DrawLabel(std::string_view text, const Rect& rect, bool focused, Align horizontal) {
    DAI_ASSERT(m_style.font != nullptr, "ui::WidgetStyle needs a font");
    float x = rect.position.x + (rect.size.x * 0.5f);
    if (horizontal == Align::Start) {
        x = rect.position.x + m_style.padding;
    } else if (horizontal == Align::End) {
        x = rect.position.x + rect.size.x - m_style.padding;
    }
    m_canvas->DrawText(*m_style.font, text, {x, rect.position.y + (rect.size.y * 0.5f)},
                       {.size = m_style.textSize,
                        .color = focused ? m_style.focusedText : m_style.text,
                        .horizontal = horizontal,
                        .vertical = Align::Center});
}

bool Context::Button(std::string_view label, const Rect& rect) {
    const Widget widget = Next(rect);
    DrawBackground(rect, widget.focused);
    DrawLabel(label, rect, widget.focused, Align::Center);
    return widget.used;
}

bool Context::Toggle(std::string_view label, const Rect& rect, bool& value) {
    const Widget widget = Next(rect);
    const bool changed = widget.used || widget.step != 0;
    if (changed) {
        value = !value;
    }
    DrawBackground(rect, widget.focused);
    DrawLabel(label, rect, widget.focused, Align::Start);
    DrawLabel(value ? "ON" : "OFF", rect, widget.focused, Align::End);
    return changed;
}

bool Context::Slider(std::string_view label, const Rect& rect, float& value, float min, float max, float step,
                     std::string_view valueText) {
    DAI_ASSERT(max > min && step > 0.0f, "A slider needs a range and a step");
    const Widget widget = Next(rect);
    const std::size_t place = m_next - 1;

    // The track fills the value area, but for the text at its right.
    const Rect values = ValueArea(rect);
    constexpr float kTextWidth = 120.0f;
    constexpr float kTrackHeight = 6.0f;
    const Rect track{{values.position.x, rect.position.y + ((rect.size.y - kTrackHeight) * 0.5f)},
                     {std::max(values.size.x - kTextWidth - m_style.padding, 1.0f), kTrackHeight}};

    const float steps = std::round((max - min) / step);
    const auto snap = [&](float wanted) {
        const float clamped = std::clamp(wanted, min, max);
        return min + (std::min(std::round((clamped - min) / step), steps) * step);
    };
    // Only input moves the value: snapping a value that came from elsewhere would change it by rounding.
    const float old = value;
    if (widget.hovered && m_click && Contains(values, m_pointer)) {
        m_dragging = place;
    }
    if (m_dragging == place && m_mouseDown) {
        const float along = std::clamp((m_pointer.x - track.position.x) / track.size.x, 0.0f, 1.0f);
        value = snap(min + (along * (max - min)));
    } else if (widget.step != 0) {
        value = snap(snap(value) + (static_cast<float>(widget.step) * step));
    }
    // A drag that ends where it started changes nothing.
    if (std::abs(value - old) <= step * 1e-3f) {
        value = old;
    }

    DrawBackground(rect, widget.focused);
    DrawLabel(label, rect, widget.focused, Align::Start);
    m_canvas->FillRect(track, m_style.track);
    const float filled = std::clamp((value - min) / (max - min), 0.0f, 1.0f);
    m_canvas->FillRect({track.position, {track.size.x * filled, track.size.y}}, m_style.accent);
    constexpr float kKnob = 22.0f;
    m_canvas->FillRect({{track.position.x + (track.size.x * filled) - (kKnob * 0.25f),
                         track.position.y + (kTrackHeight * 0.5f) - (kKnob * 0.5f)},
                        {kKnob * 0.5f, kKnob}},
                       widget.focused ? m_style.focusedText : m_style.text);
    DrawLabel(valueText, rect, widget.focused, Align::End);
    return value != old;
}

bool Context::Choice(std::string_view label, const Rect& rect, std::size_t& index,
                     std::span<const std::string_view> options) {
    DAI_ASSERT(!options.empty(), "A choice needs options");
    const Widget widget = Next(rect);
    const std::size_t count = options.size();
    const std::size_t old = std::min(index, count - 1);
    int step = widget.step;
    if (widget.used) {
        const Rect values = ValueArea(rect);
        const bool leftHalf = widget.hovered && m_click && Contains(values, m_pointer) &&
                              m_pointer.x < values.position.x + (values.size.x * 0.5f);
        step = leftHalf ? -1 : 1;
    }
    if (step > 0) {
        index = (old + 1) % count;
    } else if (step < 0) {
        index = (old + count - 1) % count;
    } else {
        index = old;
    }

    DrawBackground(rect, widget.focused);
    DrawLabel(label, rect, widget.focused, Align::Start);
    const Rect values = ValueArea(rect);
    DrawLabel("<", values, widget.focused, Align::Start);
    DrawLabel(options[index], values, widget.focused, Align::Center);
    DrawLabel(">", values, widget.focused, Align::End);
    return index != old;
}

} // namespace daidalos::ui
