// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daidalos::resources {

/// One character of a baked font. Distances are in pixels at FontData::size, with y going down from
/// the baseline.
struct GlyphData {
    char32_t codepoint = 0;
    /// How far the pen moves after the glyph.
    float advance = 0.0f;
    /// Top-left corner of the glyph's quad, from the pen on the baseline.
    float left = 0.0f;
    float top = 0.0f;
    /// Size of the quad, distance field margin included. Zero for blank glyphs such as a space.
    float width = 0.0f;
    float height = 0.0f;
    /// The quad's place in the atlas, from 0 to 1.
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

/// A change of the advance between two characters, such as "AV".
struct KerningPair {
    char32_t first = 0;
    char32_t second = 0;
    float amount = 0.0f;
};

/// A TrueType font baked into a signed distance field atlas (ADR-0027). One channel: 0.5 is the
/// outline of the glyphs, values above are inside, and the value falls from 1 to 0 over
/// 2 * distanceRange pixels across it. Text drawn at any size stays sharp.
struct FontData {
    /// Pixels from the top of the highest glyph to the bottom of the lowest, the size of the bake.
    float size = 0.0f;
    /// Above the baseline, positive.
    float ascent = 0.0f;
    /// Below the baseline, negative.
    float descent = 0.0f;
    /// Extra space between lines.
    float lineGap = 0.0f;
    /// Pixels at the bake size from the outline to where the field reaches 0 or 1.
    float distanceRange = 0.0f;
    std::uint32_t atlasWidth = 0;
    std::uint32_t atlasHeight = 0;
    /// One byte per pixel, rows from the top.
    std::vector<std::byte> atlas;
    /// Sorted by codepoint.
    std::vector<GlyphData> glyphs;
    /// Sorted by first, then second character. Pairs without kerning are left out.
    std::vector<KerningPair> kerning;

    /// The glyph of a character, or null if the font does not have it.
    [[nodiscard]] const GlyphData* Find(char32_t codepoint) const;
    /// The glyph of a character, or of '?' when it is missing, or null if '?' is missing too.
    [[nodiscard]] const GlyphData* FindOrFallback(char32_t codepoint) const;
    [[nodiscard]] float Kerning(char32_t first, char32_t second) const;
};

struct FontBakeConfig {
    float size = 48.0f;
    /// Pixels of distance field around each glyph.
    int padding = 6;
    /// Inclusive ranges of characters to bake. The default is printable ASCII and Latin-1, which covers
    /// French, and a few typographic signs.
    std::vector<std::pair<char32_t, char32_t>> ranges{{U' ', U'~'},           {U'\u00A0', U'\u00FF'},
                                                      {U'\u2013', U'\u2014'}, {U'\u2018', U'\u201D'},
                                                      {U'\u2026', U'\u2026'}, {U'\u20AC', U'\u20AC'}};
};

/// Bakes a TrueType or OpenType (TrueType outlines) font held in memory. Characters the font does
/// not have are left out. Fails with ParseError on invalid data. The name appears in error messages.
[[nodiscard]] core::Result<FontData> BakeFont(std::span<const std::byte> bytes, const FontBakeConfig& config,
                                              const std::string& name);

/// Reads and bakes a font file. A missing file is NotFound.
[[nodiscard]] core::Result<FontData> LoadFontFile(const std::filesystem::path& path, const FontBakeConfig& config = {});

/// Decodes the next character of UTF-8 text and moves past it. Invalid bytes are skipped one at a time
/// and decode as U+FFFD.
[[nodiscard]] char32_t NextCodepoint(std::string_view text, std::size_t& offset);

} // namespace daidalos::resources
