// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/rhi/Types.hpp>
#include <daidalos/ui/Font.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace daidalos::ui {

/// Canvas units across the height of the window, whatever its resolution: positions and sizes are
/// those of a 1080-pixel-high screen, scaled to the window (ADR-0027).
inline constexpr float kReferenceHeight = 1080.0f;

/// A rectangle from its top-left corner, with y going down.
struct Rect {
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
};

enum class Align {
    /// Left or top.
    Start,
    Center,
    /// Right or bottom.
    End,
};

/// Colors are sRGB, as in image editors, with straight alpha.
struct TextStyle {
    /// Canvas units from the top of the highest glyph to the bottom of the lowest.
    float size = 32.0f;
    glm::vec4 color{1.0f};
    /// Which side of the text the anchor is on.
    Align horizontal = Align::Start;
    Align vertical = Align::Start;
    /// A copy of the text drawn behind it, which keeps it readable over bright scenes. Transparent
    /// draws none.
    glm::vec4 shadowColor{0.0f};
    glm::vec2 shadowOffset{2.0f, 2.0f};
};

struct ImageStyle {
    /// The part of the texture to draw, from 0 to 1.
    Rect source{{0.0f, 0.0f}, {1.0f, 1.0f}};
    /// Multiplies the texture.
    glm::vec4 color{1.0f};
    /// Radians, clockwise on screen, around the center of the rectangle.
    float rotation = 0.0f;
};

/// How a draw colors its pixels.
enum class Shading : std::uint32_t {
    /// The vertex color only.
    Solid,
    /// The texture times the vertex color.
    Image,
    /// A font's distance field, in the vertex color.
    Text,
};

struct Vertex {
    /// Canvas units.
    glm::vec2 position{0.0f};
    glm::vec2 uv{0.0f};
    /// sRGB RGBA8, red in the lowest byte.
    std::uint32_t color = 0;
};

/// Consecutive triangles that share a texture and a shading.
struct DrawCommand {
    Shading shading = Shading::Solid;
    rhi::TextureHandle texture;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

/// The game's 2D interface for one frame, drawn over the scene after tonemapping, in the order of the
/// calls (ADR-0027). Application begins it every frame, before the handler's updates. Textures and
/// fonts must stay alive until the frame is rendered.
class Canvas {
public:
    /// Clears what was drawn and follows the size of the window, in pixels.
    void Begin(rhi::Extent pixels);

    /// Canvas units: kReferenceHeight high, and as wide as the window's aspect ratio. Zero when the
    /// window is minimized.
    [[nodiscard]] glm::vec2 Size() const { return m_size; }
    /// Window pixels per canvas unit.
    [[nodiscard]] float PixelsPerUnit() const { return m_pixelsPerUnit; }

    void FillRect(const Rect& rect, const glm::vec4& color);
    void DrawImage(rhi::TextureHandle texture, const Rect& rect, const ImageStyle& style = {});
    /// UTF-8 text, on several lines if it holds '\n'. Each line is aligned on its own. Characters the
    /// font lacks show as '?'.
    void DrawText(const Font& font, std::string_view text, glm::vec2 anchor, const TextStyle& style = {});

    [[nodiscard]] std::span<const Vertex> Vertices() const { return m_vertices; }
    [[nodiscard]] std::span<const std::uint32_t> Indices() const { return m_indices; }
    [[nodiscard]] std::span<const DrawCommand> Commands() const { return m_commands; }

private:
    void AddQuad(Shading shading, rhi::TextureHandle texture, const std::array<glm::vec2, 4>& corners,
                 const Rect& source, std::uint32_t color);
    void AddText(const Font& font, std::string_view text, glm::vec2 anchor, const TextStyle& style,
                 std::uint32_t color);

    glm::vec2 m_size{0.0f};
    float m_pixelsPerUnit = 0.0f;
    std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    std::vector<DrawCommand> m_commands;
};

/// Canvas units taken by text at a size: the widest line, and the height of all lines.
[[nodiscard]] glm::vec2 MeasureText(const Font& font, std::string_view text, float size);

/// Packs an sRGB color with straight alpha, as Vertex::color holds it.
[[nodiscard]] std::uint32_t PackColor(const glm::vec4& color);

} // namespace daidalos::ui
