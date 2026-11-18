// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/ImageFile.hpp>

#include <stb_image_write.h>

#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <fstream>
#include <string_view>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

/// Little-endian writing, which OpenEXR is in whatever the machine.
class Writer {
public:
    void Bytes(std::span<const std::byte> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }
    void Text(std::string_view text) {
        Bytes(std::as_bytes(std::span(text.data(), text.size())));
        m_bytes.push_back(std::byte{0});
    }
    template <typename T>
    void Value(T value) {
        static_assert(std::endian::native == std::endian::little, "OpenEXR writing assumes a little-endian machine");
        std::array<std::byte, sizeof(T)> raw{};
        std::memcpy(raw.data(), &value, sizeof(T));
        Bytes(raw);
    }
    /// An attribute's header: its name, its type and the size of its value.
    void Attribute(std::string_view name, std::string_view type, std::uint32_t size) {
        Text(name);
        Text(type);
        Value(size);
    }
    [[nodiscard]] std::size_t Size() const { return m_bytes.size(); }
    std::vector<std::byte>& Data() { return m_bytes; }

private:
    std::vector<std::byte> m_bytes;
};

} // namespace

core::Result<std::vector<std::byte>> EncodePng(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::uint8_t> rgba) {
    if (width == 0 || height == 0 || rgba.size() != std::size_t{width} * height * 4) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("A {}x{} PNG needs {} bytes of RGBA, not {}", width, height,
                                           std::size_t{width} * height * 4, rgba.size()));
    }
    std::vector<std::byte> png;
    const auto append = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<std::byte>*>(context);
        const auto* bytes = static_cast<const std::byte*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    if (stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                               static_cast<int>(width * 4)) == 0) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule, "Cannot encode the PNG");
    }
    return png;
}

core::Result<std::vector<std::byte>> EncodeExr(std::uint32_t width, std::uint32_t height, std::span<const float> rgb) {
    if (width == 0 || height == 0 || rgb.size() != std::size_t{width} * height * 3) {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("A {}x{} EXR needs {} floats of RGB, not {}", width, height,
                                           std::size_t{width} * height * 3, rgb.size()));
    }
    constexpr std::uint32_t kFloat = 2; // PIXELTYPE_FLOAT
    Writer out;
    out.Value<std::uint32_t>(20000630); // The magic number.
    out.Value<std::uint32_t>(2);        // Version 2, one part, scanlines.

    // Channels in alphabetical order, as the format wants: name, pixel type, linear, reserved, sampling.
    constexpr std::array<std::string_view, 3> kChannels{"B", "G", "R"};
    out.Attribute("channels", "chlist", (static_cast<std::uint32_t>(kChannels.size()) * 18) + 1);
    for (const std::string_view channel : kChannels) {
        out.Text(channel);
        out.Value<std::uint32_t>(kFloat);
        out.Value<std::uint8_t>(0);
        out.Bytes(std::array<std::byte, 3>{});
        out.Value<std::int32_t>(1);
        out.Value<std::int32_t>(1);
    }
    out.Value<std::uint8_t>(0);
    out.Attribute("compression", "compression", 1);
    out.Value<std::uint8_t>(0); // None.
    const auto box = [&](std::string_view name) {
        out.Attribute(name, "box2i", 16);
        out.Value<std::int32_t>(0);
        out.Value<std::int32_t>(0);
        out.Value(static_cast<std::int32_t>(width) - 1);
        out.Value(static_cast<std::int32_t>(height) - 1);
    };
    box("dataWindow");
    box("displayWindow");
    out.Attribute("lineOrder", "lineOrder", 1);
    out.Value<std::uint8_t>(0); // Increasing y.
    out.Attribute("pixelAspectRatio", "float", 4);
    out.Value(1.0f);
    out.Attribute("screenWindowCenter", "v2f", 8);
    out.Value(0.0f);
    out.Value(0.0f);
    out.Attribute("screenWindowWidth", "float", 4);
    out.Value(1.0f);
    out.Value<std::uint8_t>(0); // End of the header.

    // The offset table, one per scanline, then the scanlines: y, size, and each channel's row in turn.
    const std::size_t lineBytes = std::size_t{width} * kChannels.size() * sizeof(float);
    const std::size_t first = out.Size() + (std::size_t{height} * sizeof(std::uint64_t));
    for (std::uint32_t y = 0; y < height; ++y) {
        out.Value<std::uint64_t>(first + (y * (lineBytes + 8)));
    }
    out.Data().reserve(first + (std::size_t{height} * (lineBytes + 8)));
    for (std::uint32_t y = 0; y < height; ++y) {
        out.Value(static_cast<std::int32_t>(y));
        out.Value(static_cast<std::uint32_t>(lineBytes));
        for (const std::size_t channel : {std::size_t{2}, std::size_t{1}, std::size_t{0}}) {
            for (std::uint32_t x = 0; x < width; ++x) {
                out.Value(rgb[(((std::size_t{y} * width) + x) * 3) + channel]);
            }
        }
    }
    return std::move(out.Data());
}

core::Result<void> WriteFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, kModule, std::format("Cannot write '{}'", path.string()));
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, kModule, std::format("Cannot write '{}'", path.string()));
    }
    return {};
}

} // namespace daidalos::resources
