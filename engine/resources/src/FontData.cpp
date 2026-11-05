// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/resources/AssetPath.hpp>
#include <daidalos/resources/FontData.hpp>

#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";
constexpr char32_t kReplacement = U'\uFFFD';
/// Pixels left empty between glyphs, so that filtering does not bleed one into the next.
constexpr int kSpacing = 1;
constexpr std::uint32_t kMaxAtlasSize = 4096;

struct SdfBitmap {
    struct Free {
        void operator()(unsigned char* bitmap) const { stbtt_FreeSDF(bitmap, nullptr); }
    };
    std::unique_ptr<unsigned char, Free> pixels;
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
};

struct BakedGlyph {
    int index = 0;
    GlyphData data;
    SdfBitmap bitmap;
};

/// Places every bitmap in the smallest atlas, doubling its sides in turn. False if none is large enough.
bool Pack(std::vector<BakedGlyph>& glyphs, std::vector<stbrp_rect>& rects, std::uint32_t& width,
          std::uint32_t& height) {
    rects.clear();
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const SdfBitmap& bitmap = glyphs[index].bitmap;
        if (bitmap.width > 0 && bitmap.height > 0) {
            rects.push_back({.id = static_cast<int>(index),
                             .w = bitmap.width + kSpacing,
                             .h = bitmap.height + kSpacing,
                             .x = 0,
                             .y = 0,
                             .was_packed = 0});
        }
    }
    width = 256;
    height = 256;
    while (width <= kMaxAtlasSize && height <= kMaxAtlasSize) {
        std::vector<stbrp_node> nodes(width);
        stbrp_context context;
        stbrp_init_target(&context, static_cast<int>(width), static_cast<int>(height), nodes.data(),
                          static_cast<int>(nodes.size()));
        if (stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size())) == 1) {
            return true;
        }
        if (width == height) {
            width *= 2;
        } else {
            height *= 2;
        }
    }
    return false;
}

} // namespace

const GlyphData* FontData::Find(char32_t codepoint) const {
    const auto found = std::lower_bound(glyphs.begin(), glyphs.end(), codepoint,
                                        [](const GlyphData& glyph, char32_t value) { return glyph.codepoint < value; });
    return (found != glyphs.end() && found->codepoint == codepoint) ? &*found : nullptr;
}

const GlyphData* FontData::FindOrFallback(char32_t codepoint) const {
    const GlyphData* glyph = Find(codepoint);
    return glyph != nullptr ? glyph : Find(U'?');
}

float FontData::Kerning(char32_t first, char32_t second) const {
    const auto found = std::lower_bound(kerning.begin(), kerning.end(), std::pair(first, second),
                                        [](const KerningPair& pair, const std::pair<char32_t, char32_t>& value) {
                                            return std::pair(pair.first, pair.second) < value;
                                        });
    return (found != kerning.end() && found->first == first && found->second == second) ? found->amount : 0.0f;
}

core::Result<FontData> BakeFont(std::span<const std::byte> bytes, const FontBakeConfig& config,
                                const std::string& name) {
    DAI_PROFILE_SCOPE("resources::BakeFont");
    if (config.size <= 0.0f || config.padding < 1) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("Invalid size or padding to bake font '{}'", name));
    }
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    stbtt_fontinfo info{};
    const int offset = bytes.empty() ? -1 : stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || stbtt_InitFont(&info, data, offset) == 0) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Font '{}' is not a TrueType or OpenType font", name));
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, config.size);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    // Distances fall by 128 / padding per pixel, from 128 on the outline: 0 at the edge of the margin.
    const float distanceScale = 128.0f / static_cast<float>(config.padding);
    std::vector<BakedGlyph> glyphs;
    for (const auto& [first, last] : config.ranges) {
        for (char32_t codepoint = first; codepoint <= last; ++codepoint) {
            const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
            if (index == 0) {
                continue;
            }
            BakedGlyph glyph;
            glyph.index = index;
            glyph.data.codepoint = codepoint;
            int advance = 0;
            int bearing = 0;
            stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);
            glyph.data.advance = static_cast<float>(advance) * scale;
            SdfBitmap& bitmap = glyph.bitmap;
            bitmap.pixels.reset(stbtt_GetGlyphSDF(&info, scale, index, config.padding, 128, distanceScale,
                                                  &bitmap.width, &bitmap.height, &bitmap.left, &bitmap.top));
            if (!bitmap.pixels) {
                bitmap.width = 0;
                bitmap.height = 0;
            }
            glyphs.push_back(std::move(glyph));
        }
    }
    std::sort(glyphs.begin(), glyphs.end(),
              [](const BakedGlyph& a, const BakedGlyph& b) { return a.data.codepoint < b.data.codepoint; });
    glyphs.erase(
        std::unique(glyphs.begin(), glyphs.end(),
                    [](const BakedGlyph& a, const BakedGlyph& b) { return a.data.codepoint == b.data.codepoint; }),
        glyphs.end());
    if (glyphs.empty()) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Font '{}' has none of the characters to bake", name));
    }

    FontData font;
    std::vector<stbrp_rect> rects;
    if (!Pack(glyphs, rects, font.atlasWidth, font.atlasHeight)) {
        return core::MakeError(
            core::ErrorCode::NotSupported, kModule,
            std::format("Font '{}' does not fit in a {} pixel atlas at size {}", name, kMaxAtlasSize, config.size));
    }
    font.size = config.size;
    font.ascent = static_cast<float>(ascent) * scale;
    font.descent = static_cast<float>(descent) * scale;
    font.lineGap = static_cast<float>(lineGap) * scale;
    font.distanceRange = static_cast<float>(config.padding);
    font.atlas.resize(static_cast<std::size_t>(font.atlasWidth) * font.atlasHeight);
    const auto atlasWidth = static_cast<float>(font.atlasWidth);
    const auto atlasHeight = static_cast<float>(font.atlasHeight);
    for (const stbrp_rect& rect : rects) {
        BakedGlyph& glyph = glyphs[static_cast<std::size_t>(rect.id)];
        const SdfBitmap& bitmap = glyph.bitmap;
        for (int row = 0; row < bitmap.height; ++row) {
            std::memcpy(&font.atlas[(static_cast<std::size_t>(rect.y + row) * font.atlasWidth) +
                                    static_cast<std::size_t>(rect.x)],
                        bitmap.pixels.get() + (static_cast<std::ptrdiff_t>(row) * bitmap.width),
                        static_cast<std::size_t>(bitmap.width));
        }
        GlyphData& placed = glyph.data;
        placed.left = static_cast<float>(bitmap.left);
        placed.top = static_cast<float>(bitmap.top);
        placed.width = static_cast<float>(bitmap.width);
        placed.height = static_cast<float>(bitmap.height);
        placed.u0 = static_cast<float>(rect.x) / atlasWidth;
        placed.v0 = static_cast<float>(rect.y) / atlasHeight;
        placed.u1 = static_cast<float>(rect.x + bitmap.width) / atlasWidth;
        placed.v1 = static_cast<float>(rect.y + bitmap.height) / atlasHeight;
    }

    font.glyphs.reserve(glyphs.size());
    for (const BakedGlyph& glyph : glyphs) {
        font.glyphs.push_back(glyph.data);
    }
    // Sorted by construction, as the glyphs are.
    for (const BakedGlyph& first : glyphs) {
        for (const BakedGlyph& second : glyphs) {
            if (const int amount = stbtt_GetGlyphKernAdvance(&info, first.index, second.index); amount != 0) {
                font.kerning.push_back(
                    {first.data.codepoint, second.data.codepoint, static_cast<float>(amount) * scale});
            }
        }
    }
    return font;
}

core::Result<FontData> LoadFontFile(const std::filesystem::path& path, const FontBakeConfig& config) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return core::MakeError(core::ErrorCode::NotFound, kModule,
                               std::format("Cannot open font file '{}'", ToUtf8(path)));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot read font file '{}'", ToUtf8(path)));
    }
    return BakeFont(bytes, config, ToUtf8(path));
}

char32_t NextCodepoint(std::string_view text, std::size_t& offset) {
    const auto byte = [&](std::size_t index) { return static_cast<std::uint8_t>(text[index]); };
    const std::uint8_t lead = byte(offset);
    std::size_t length = 0;
    char32_t codepoint = 0;
    if (lead < 0x80) {
        ++offset;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        codepoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        codepoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        codepoint = lead & 0x07;
    } else {
        ++offset;
        return kReplacement;
    }
    if (offset + length > text.size()) {
        ++offset;
        return kReplacement;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint8_t next = byte(offset + index);
        if ((next & 0xC0) != 0x80) {
            ++offset;
            return kReplacement;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }
    // Overlong forms, surrogates and values past Unicode are invalid.
    constexpr std::array<char32_t, 5> kMinimum{0, 0, 0x80, 0x800, 0x10000};
    if (codepoint < kMinimum[length] || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        ++offset;
        return kReplacement;
    }
    offset += length;
    return codepoint;
}

} // namespace daidalos::resources
