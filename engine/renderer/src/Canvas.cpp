// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ui/Canvas.hpp>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace daidalos::ui {

namespace {

/// The share of the free space that goes before what is aligned.
float AlignFactor(Align align) {
    switch (align) {
    case Align::Center:
        return 0.5f;
    case Align::End:
        return 1.0f;
    default:
        return 0.0f;
    }
}

float LineStep(const resources::FontData& font, float scale) {
    return (font.ascent - font.descent + font.lineGap) * scale;
}

/// Width of one line, without its newline, in canvas units.
float LineWidth(const resources::FontData& font, std::string_view line, float scale) {
    float width = 0.0f;
    char32_t previous = 0;
    for (std::size_t offset = 0; offset < line.size();) {
        const char32_t codepoint = resources::NextCodepoint(line, offset);
        const resources::GlyphData* glyph = font.FindOrFallback(codepoint);
        if (glyph == nullptr) {
            continue;
        }
        width += (font.Kerning(previous, glyph->codepoint) + glyph->advance) * scale;
        previous = glyph->codepoint;
    }
    return width;
}

/// Calls the function with each line of the text, without its newline.
template <class Function>
void ForEachLine(std::string_view text, Function&& function) {
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        function(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return;
        }
        start = end + 1;
    }
}

} // namespace

std::uint32_t PackColor(const glm::vec4& color) {
    const glm::vec4 bytes = glm::round(glm::clamp(color, 0.0f, 1.0f) * 255.0f);
    return static_cast<std::uint32_t>(bytes.r) | (static_cast<std::uint32_t>(bytes.g) << 8U) |
           (static_cast<std::uint32_t>(bytes.b) << 16U) | (static_cast<std::uint32_t>(bytes.a) << 24U);
}

glm::vec2 MeasureText(const Font& font, std::string_view text, float size) {
    const resources::FontData& data = font.data;
    if (data.size <= 0.0f) {
        return glm::vec2(0.0f);
    }
    const float scale = size / data.size;
    float width = 0.0f;
    int lines = 0;
    ForEachLine(text, [&](std::string_view line) {
        width = std::max(width, LineWidth(data, line, scale));
        ++lines;
    });
    return {width, size + (static_cast<float>(lines - 1) * LineStep(data, scale))};
}

void Canvas::Begin(rhi::Extent pixels) {
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    if (pixels.width == 0 || pixels.height == 0) {
        m_size = glm::vec2(0.0f);
        m_pixelsPerUnit = 0.0f;
        return;
    }
    m_pixelsPerUnit = static_cast<float>(pixels.height) / kReferenceHeight;
    m_size = {static_cast<float>(pixels.width) / m_pixelsPerUnit, kReferenceHeight};
}

void Canvas::AddQuad(Shading shading, rhi::TextureHandle texture, const std::array<glm::vec2, 4>& corners,
                     const Rect& source, std::uint32_t color) {
    if ((color >> 24U) == 0) {
        return; // Fully transparent.
    }
    if (m_commands.empty() || m_commands.back().shading != shading || m_commands.back().texture != texture) {
        m_commands.push_back({shading, texture, static_cast<std::uint32_t>(m_indices.size()), 0});
    }
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    const glm::vec2 uv0 = source.position;
    const glm::vec2 uv1 = source.position + source.size;
    // Corners go clockwise on screen from the top left.
    m_vertices.push_back({corners[0], uv0, color});
    m_vertices.push_back({corners[1], {uv1.x, uv0.y}, color});
    m_vertices.push_back({corners[2], uv1, color});
    m_vertices.push_back({corners[3], {uv0.x, uv1.y}, color});
    for (const std::uint32_t index : {0U, 1U, 2U, 0U, 2U, 3U}) {
        m_indices.push_back(first + index);
    }
    m_commands.back().indexCount += 6;
}

void Canvas::FillRect(const Rect& rect, const glm::vec4& color) {
    const glm::vec2 min = rect.position;
    const glm::vec2 max = rect.position + rect.size;
    AddQuad(Shading::Solid, {}, {min, {max.x, min.y}, max, {min.x, max.y}}, {}, PackColor(color));
}

void Canvas::DrawImage(rhi::TextureHandle texture, const Rect& rect, const ImageStyle& style) {
    const glm::vec2 half = rect.size * 0.5f;
    const glm::vec2 center = rect.position + half;
    std::array<glm::vec2, 4> corners{-half, glm::vec2(half.x, -half.y), half, glm::vec2(-half.x, half.y)};
    // With y going down, a positive angle turns clockwise on screen.
    const float cosine = std::cos(style.rotation);
    const float sine = std::sin(style.rotation);
    for (glm::vec2& corner : corners) {
        corner = center + glm::vec2(corner.x * cosine - corner.y * sine, corner.x * sine + corner.y * cosine);
    }
    AddQuad(Shading::Image, texture, corners, style.source, PackColor(style.color));
}

void Canvas::DrawText(const Font& font, std::string_view text, glm::vec2 anchor, const TextStyle& style) {
    if (style.shadowColor.a > 0.0f) {
        AddText(font, text, anchor + style.shadowOffset, style, PackColor(style.shadowColor));
    }
    AddText(font, text, anchor, style, PackColor(style.color));
}

void Canvas::AddText(const Font& font, std::string_view text, glm::vec2 anchor, const TextStyle& style,
                     std::uint32_t color) {
    const resources::FontData& data = font.data;
    if (data.size <= 0.0f) {
        return;
    }
    const float scale = style.size / data.size;
    const glm::vec2 size = MeasureText(font, text, style.size);
    float lineTop = anchor.y - (size.y * AlignFactor(style.vertical));
    const float step = LineStep(data, scale);
    ForEachLine(text, [&](std::string_view line) {
        glm::vec2 pen(anchor.x - (LineWidth(data, line, scale) * AlignFactor(style.horizontal)),
                      lineTop + (data.ascent * scale));
        char32_t previous = 0;
        for (std::size_t offset = 0; offset < line.size();) {
            const resources::GlyphData* glyph = data.FindOrFallback(resources::NextCodepoint(line, offset));
            if (glyph == nullptr) {
                continue;
            }
            pen.x += data.Kerning(previous, glyph->codepoint) * scale;
            previous = glyph->codepoint;
            if (glyph->width > 0.0f) {
                const glm::vec2 min = pen + (glm::vec2(glyph->left, glyph->top) * scale);
                const glm::vec2 max = min + (glm::vec2(glyph->width, glyph->height) * scale);
                AddQuad(Shading::Text, font.atlas, {min, {max.x, min.y}, max, {min.x, max.y}},
                        {{glyph->u0, glyph->v0}, {glyph->u1 - glyph->u0, glyph->v1 - glyph->v0}}, color);
            }
            pen.x += glyph->advance * scale;
        }
        lineTop += step;
    });
}

core::Result<rhi::TextureHandle> CreateFontAtlas(rhi::Device& device, const resources::FontData& font,
                                                 const std::string& name) {
    DAI_TRY_ASSIGN(rhi::TextureHandle texture,
                   device.CreateTexture({.extent = {font.atlasWidth, font.atlasHeight},
                                         .format = rhi::Format::R8Unorm,
                                         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                                         .name = std::format("Font {}", name)}));
    if (core::Result<void> uploaded = device.UploadTexture(texture, font.atlas); !uploaded) {
        device.DestroyTexture(texture);
        return tl::unexpected(uploaded.error());
    }
    return texture;
}

} // namespace daidalos::ui
