// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ui/Context.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <variant>
#include <vector>

using namespace daidalos;
using platform::Key;
using platform::MouseButton;

namespace {

ui::Font OneGlyphFont() {
    ui::Font font;
    font.data.size = 10.0f;
    font.data.ascent = 8.0f;
    font.data.descent = -2.0f;
    font.data.glyphs = {{.codepoint = U'?', .advance = 10.0f, .width = 8.0f, .height = 10.0f}};
    return font;
}

/// Widgets on a 1920x1080 canvas, in a window of the same size in mouse units, fed one frame at a time.
class ContextTest : public ::testing::Test {
protected:
    ContextTest() {
        m_font = OneGlyphFont();
        m_context.SetStyle({.font = &m_font});
    }

    /// Starts a frame with these events. Keys pressed in the previous frame are released first, so that
    /// they can be pressed again; the mouse button stays down until released.
    void Frame(std::initializer_list<platform::Event> events = {}) {
        m_input.BeginFrame();
        for (const Key key : m_held) {
            m_input.Process(platform::KeyReleased{.key = key});
        }
        m_held.clear();
        for (const platform::Event& event : events) {
            m_input.Process(event);
            if (const auto* press = std::get_if<platform::KeyPressed>(&event)) {
                m_held.push_back(press->key);
            }
        }
        m_canvas.Begin({1920, 1080});
        m_context.Begin(m_canvas, m_input, {1920.0f, 1080.0f});
    }

    static platform::Event Press(Key key) { return platform::KeyPressed{.key = key}; }
    static platform::Event Move(glm::vec2 position) {
        return platform::MouseMoved{.position = position, .delta = {1.0f, 0.0f}};
    }
    static platform::Event Click(glm::vec2 position) {
        return platform::MouseButtonPressed{.button = MouseButton::Left, .position = position};
    }
    static platform::Event Release(glm::vec2 position) {
        return platform::MouseButtonReleased{.button = MouseButton::Left, .position = position};
    }

    /// Three buttons in a column: which of them were used this frame.
    std::array<bool, 3> Buttons() {
        return {m_context.Button("A", Row(0)), m_context.Button("B", Row(1)), m_context.Button("C", Row(2))};
    }

    static ui::Rect Row(int index) {
        return {{100.0f, 100.0f + (100.0f * static_cast<float>(index))}, {800.0f, 80.0f}};
    }
    static glm::vec2 Inside(int row) { return {500.0f, 140.0f + (100.0f * static_cast<float>(row))}; }

    ui::Font m_font;
    ui::Canvas m_canvas;
    ui::Context m_context;
    input::InputState m_input;
    std::vector<Key> m_held;
};

} // namespace

TEST_F(ContextTest, ClickUsesTheButtonUnderThePointerOnly) {
    Frame({Move(Inside(1)), Click(Inside(1))});
    EXPECT_EQ(Buttons(), (std::array{false, true, false}));
    Frame({Click({1500.0f, 140.0f})});
    EXPECT_EQ(Buttons(), (std::array{false, false, false}));
}

TEST_F(ContextTest, PointerIsPlacedFromWindowUnitsOnTheCanvas) {
    m_input.Process(Move({320.0f, 180.0f}));
    m_canvas.Begin({1280, 720});
    m_context.Begin(m_canvas, m_input, {640.0f, 360.0f});
    EXPECT_FLOAT_EQ(m_context.Pointer().x, 960.0f);
    EXPECT_FLOAT_EQ(m_context.Pointer().y, 540.0f);
}

TEST_F(ContextTest, ArrowsMoveTheFocusAroundAndEnterUsesIt) {
    Frame();
    Buttons();
    Frame({Press(Key::Down)});
    Buttons();
    Frame({Press(Key::Enter)});
    EXPECT_EQ(Buttons(), (std::array{false, true, false}));
    Frame({Press(Key::Up)});
    Buttons();
    Frame({Press(Key::Up)});
    Buttons();
    Frame({Press(Key::Space)});
    EXPECT_EQ(Buttons(), (std::array{false, false, true})) << "Up from the first wraps to the last";
}

TEST_F(ContextTest, HoverTakesTheFocusOnlyWhenThePointerMoves) {
    Frame({Move(Inside(2))});
    Buttons();
    Frame({Press(Key::Up)});
    Buttons();
    Frame({Press(Key::Enter)});
    EXPECT_EQ(Buttons(), (std::array{false, true, false})) << "The still pointer leaves the keyboard's focus alone";
}

TEST_F(ContextTest, ResetFocusIgnoresTheRestOfTheFrame) {
    Frame({Move(Inside(0)), Click(Inside(0))});
    ASSERT_TRUE(m_context.Button("A", Row(0)));
    m_context.ResetFocus();
    EXPECT_FALSE(m_context.Button("Next screen", Row(0))) << "The same click must not go through";
    Frame({Press(Key::Enter)});
    EXPECT_TRUE(m_context.Button("Next screen", Row(0)));
}

TEST_F(ContextTest, ToggleFlipsWhenUsedOrMovedSideways) {
    bool value = false;
    Frame({Press(Key::Enter)});
    EXPECT_TRUE(m_context.Toggle("Vsync", Row(0), value));
    EXPECT_TRUE(value);
    Frame({Press(Key::Left)});
    EXPECT_TRUE(m_context.Toggle("Vsync", Row(0), value));
    EXPECT_FALSE(value);
    Frame();
    EXPECT_FALSE(m_context.Toggle("Vsync", Row(0), value));
}

TEST_F(ContextTest, SliderStepsWithArrowsWithinItsRange) {
    float value = 0.9f;
    Frame({Press(Key::Right)});
    EXPECT_TRUE(m_context.Slider("Volume", Row(0), value, 0.0f, 1.0f, 0.1f, ""));
    EXPECT_FLOAT_EQ(value, 1.0f);
    Frame({Press(Key::Right)});
    EXPECT_FALSE(m_context.Slider("Volume", Row(0), value, 0.0f, 1.0f, 0.1f, ""));
    EXPECT_FLOAT_EQ(value, 1.0f);
    Frame({Press(Key::Left)});
    m_context.Slider("Volume", Row(0), value, 0.0f, 1.0f, 0.1f, "");
    EXPECT_NEAR(value, 0.9f, 1e-6f);
}

TEST_F(ContextTest, SliderLeavesItsValueAloneWithoutInput) {
    // 0.0025 is on the grid of 0.0005 plus steps of 0.00025, but not exactly in floats.
    float value = 0.0025f;
    Frame({Move(Inside(0))});
    EXPECT_FALSE(m_context.Slider("Mouse", Row(0), value, 0.0005f, 0.008f, 0.00025f, ""));
    EXPECT_EQ(value, 0.0025f);
}

TEST_F(ContextTest, SliderFollowsThePointerWhileDragged) {
    // The row spans 100 to 900; its track starts halfway, at 500, and leaves 144 units for the text.
    float value = 0.0f;
    const glm::vec2 start{500.0f, 140.0f};
    Frame({Move(start), Click(start)});
    m_context.Slider("Volume", Row(0), value, 0.0f, 100.0f, 1.0f, "");
    EXPECT_FLOAT_EQ(value, 0.0f);
    Frame({Move({628.0f, 400.0f})});
    EXPECT_TRUE(m_context.Slider("Volume", Row(0), value, 0.0f, 100.0f, 1.0f, ""));
    EXPECT_FLOAT_EQ(value, 50.0f) << "Dragging goes on outside the row";
    Frame({Release({628.0f, 400.0f}), Move({756.0f, 140.0f})});
    EXPECT_FALSE(m_context.Slider("Volume", Row(0), value, 0.0f, 100.0f, 1.0f, ""));
}

TEST_F(ContextTest, ChoiceWrapsBothWays) {
    constexpr std::array<std::string_view, 3> kOptions{"Off", "FXAA", "MSAA"};
    std::size_t index = 2;
    Frame({Press(Key::Enter)});
    EXPECT_TRUE(m_context.Choice("Antialiasing", Row(0), index, kOptions));
    EXPECT_EQ(index, 0u);
    Frame({Press(Key::Left)});
    m_context.Choice("Antialiasing", Row(0), index, kOptions);
    EXPECT_EQ(index, 2u);

    // The left half of the option goes back, the right half forward.
    Frame({Move({550.0f, 140.0f}), Click({550.0f, 140.0f})});
    m_context.Choice("Antialiasing", Row(0), index, kOptions);
    EXPECT_EQ(index, 1u);
    Frame({Release({550.0f, 140.0f}), Click({850.0f, 140.0f})});
    m_context.Choice("Antialiasing", Row(0), index, kOptions);
    EXPECT_EQ(index, 2u);
}

TEST_F(ContextTest, AGamepadMovesWithItsDpadOrStickAndUsesWithSouth) {
    constexpr auto kPad = static_cast<platform::GamepadId>(2);
    using platform::GamepadButton;
    m_input.Process(platform::GamepadConnected{kPad});
    Frame();
    Buttons();
    Frame({platform::GamepadButtonPressed{kPad, GamepadButton::DpadDown}});
    Buttons();
    Frame({platform::GamepadButtonReleased{kPad, GamepadButton::DpadDown},
           platform::GamepadButtonPressed{kPad, GamepadButton::South}});
    EXPECT_EQ(Buttons(), (std::array{false, true, false}));
    // A lean of the stick moves once, however long it is held.
    Frame({platform::GamepadButtonReleased{kPad, GamepadButton::South},
           platform::GamepadAxisMoved{kPad, platform::GamepadAxis::LeftY, -0.9f}});
    Buttons();
    Frame();
    Buttons();
    Frame({platform::GamepadAxisMoved{kPad, platform::GamepadAxis::LeftY, 0.0f},
           platform::GamepadButtonPressed{kPad, GamepadButton::South}});
    EXPECT_EQ(Buttons(), (std::array{false, false, true}));
}

TEST_F(ContextTest, HeldWidgetsReadNoInputAndKeepTheFocus) {
    Frame();
    Buttons();
    Frame({Press(Key::Down)});
    Buttons();
    m_context.Hold(true);
    Frame({Press(Key::Down)});
    EXPECT_EQ(Buttons(), (std::array{false, false, false}));
    Frame({Press(Key::Enter), Move(Inside(0)), Click(Inside(0))});
    EXPECT_EQ(Buttons(), (std::array{false, false, false}));
    m_context.Hold(false);
    Frame({Release(Inside(0))});
    Buttons();
    Frame({Press(Key::Enter)});
    EXPECT_EQ(Buttons(), (std::array{false, true, false})) << "The focus stayed where it was before the hold";
}
