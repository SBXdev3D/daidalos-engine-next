// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Converts a PNG or JPEG texture to a KTX2 file that the engine loads compressed (ADR-0046):
//
//     texture-tool color <albedo.png> <out.ktx2>
//     texture-tool normal <normal.png> <out.ktx2>
//     texture-tool data <map.png> <out.ktx2>
//     texture-tool metallic-roughness <roughness.png> [<metallic.png>] <out.ktx2>
//
// With --compare first, the KTX2 file is read instead of written, and compared with what its sources would
// encode: the PSNR of each channel at full size and, for normal maps, the angle between the normals.
//
// metallic-roughness packs grayscale maps the way glTF reads them: roughness in green, metalness in blue.
// Without a metalness map, blue is full, so that the material's metallicFactor alone decides.
//
//     texture-tool flipbook fire|smoke|explosion <out> [--frames N] [--size N] [--grid N] [--seed N]
//                  [--preview 1]
//
// simulates and renders a flipbook (ADR-0053) and writes <out>.ktx2, its color and coverage, and
// <out>_lp.ktx2 and <out>_ln.ktx2, its six-way light maps, then prints the grid an effect file gives them.
// With --preview 1, it also writes the three as PNG files, to look at.

#include <daidalos/core/Result.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <ktx.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Flipbook.hpp"

using namespace daidalos;
using resources::ImageData;
using resources::TextureUse;

namespace {

constexpr std::string_view kUsage =
    "usage: texture-tool [--compare] color|normal|data <input> <file.ktx2>\n"
    "       texture-tool [--compare] metallic-roughness <roughness> [<metallic>] <file.ktx2>\n"
    "       texture-tool flipbook fire|smoke|explosion <out> [--frames N] [--size N] [--grid N] [--seed N]\n"
    "                    [--preview 1]\n";

// glTF metallic-roughness texture: red is unused, green is roughness, blue is metalness.
core::Result<ImageData> PackMetallicRoughness(const ImageData& roughness, const ImageData* metallic) {
    if (metallic != nullptr && (metallic->width != roughness.width || metallic->height != roughness.height)) {
        return core::MakeError(core::ErrorCode::InvalidArgument, "texture-tool",
                               "The roughness and metalness maps differ in size");
    }
    ImageData packed = roughness;
    for (std::size_t texel = 0; texel < packed.pixels.size(); texel += 4) {
        packed.pixels[texel] = std::byte{255};
        packed.pixels[texel + 1] = roughness.pixels[texel];
        packed.pixels[texel + 2] = (metallic != nullptr) ? metallic->pixels[texel] : std::byte{255};
        packed.pixels[texel + 3] = std::byte{255};
    }
    return packed;
}

core::Result<ImageData> Load(std::span<char* const> arguments, bool packing, TextureUse use) {
    if (packing) {
        DAI_TRY_ASSIGN(const ImageData roughness, resources::LoadImageFile(arguments[0], TextureUse::Data));
        if (arguments.size() == 1) {
            return PackMetallicRoughness(roughness, nullptr);
        }
        DAI_TRY_ASSIGN(const ImageData metallic, resources::LoadImageFile(arguments[1], TextureUse::Data));
        return PackMetallicRoughness(roughness, &metallic);
    }
    return resources::LoadImageFile(arguments[0], use);
}

// A normal of a normal map, rebuilt from red and green as the shaders do.
std::array<double, 3> Normal(std::uint8_t red, std::uint8_t green) {
    const double x = red / 127.5 - 1.0;
    const double y = green / 127.5 - 1.0;
    return {x, y, std::sqrt(std::max(0.0, 1.0 - x * x - y * y))};
}

double Angle(const std::array<double, 3>& n, const std::array<double, 3>& m) {
    const double lengths =
        std::sqrt((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]));
    const double cosine = (n[0] * m[0] + n[1] * m[1] + n[2] * m[2]) / std::max(lengths, 1e-9);
    return std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
}

// Compares the full-size level of a KTX2 file, decoded, with the image it was encoded from.
core::Result<std::string> Compare(const ImageData& source, const std::filesystem::path& path, TextureUse use) {
    ktxTexture2* created = nullptr;
    const ktx_error_code_e opened =
        ktxTexture2_CreateFromNamedFile(path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &created);
    if (opened != KTX_SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, "texture-tool",
                               std::format("'{}': {}", path.string(), ktxErrorString(opened)));
    }
    const auto destroy = [](ktxTexture2* texture) { ktxTexture_Destroy(ktxTexture(texture)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> texture(created, destroy);
    if (ktxTexture2_NeedsTranscoding(texture.get()) &&
        ktxTexture2_TranscodeBasis(texture.get(), KTX_TTF_RGBA32, 0) != KTX_SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, "texture-tool", "Cannot decode the KTX2 file");
    }
    if (texture->baseWidth != source.width || texture->baseHeight != source.height) {
        return core::MakeError(core::ErrorCode::InvalidArgument, "texture-tool", "The sizes differ");
    }
    ktx_size_t offset = 0;
    ktxTexture_GetImageOffset(ktxTexture(texture.get()), 0, 0, 0, &offset);
    const ktx_uint8_t* decoded = ktxTexture_GetData(ktxTexture(texture.get())) + offset;
    const auto* original = reinterpret_cast<const std::uint8_t*>(source.pixels.data());

    const std::size_t texels = std::size_t{source.width} * source.height;
    std::array<double, 3> squared{};
    double angleSum = 0.0;
    double angleMax = 0.0;
    // libktx encodes normal maps as RRRG: green is in alpha, and blue is rebuilt by the shaders.
    const bool normal = use == TextureUse::Normal;
    const std::size_t channels = normal ? 2 : 3;
    for (std::size_t texel = 0; texel < texels; ++texel) {
        const std::uint8_t* a = original + texel * 4;
        const std::array<std::uint8_t, 4> raw{decoded[texel * 4], decoded[texel * 4 + 1], decoded[texel * 4 + 2],
                                              decoded[texel * 4 + 3]};
        const std::array<std::uint8_t, 4> b = normal ? std::array<std::uint8_t, 4>{raw[0], raw[3], 0, 255} : raw;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const double difference = static_cast<double>(a[channel]) - b[channel];
            squared[channel] += difference * difference;
        }
        if (normal) {
            const double angle = Angle(Normal(a[0], a[1]), Normal(b[0], b[1]));
            angleSum += angle;
            angleMax = std::max(angleMax, angle);
        }
    }
    const auto psnr = [texels](double sum) {
        return (sum == 0.0) ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / (sum / static_cast<double>(texels)));
    };
    std::string report =
        std::format("{}: PSNR R {:.1f} dB, G {:.1f} dB", path.filename().string(), psnr(squared[0]), psnr(squared[1]));
    if (!normal) {
        report += std::format(", B {:.1f} dB", psnr(squared[2]));
    } else {
        report += std::format(", normals {:.2f} degrees apart on average, {:.1f} at most",
                              angleSum / static_cast<double>(texels), angleMax);
    }
    return report;
}

core::Result<void> Write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, "texture-tool",
                               std::format("Cannot write '{}'", path.string()));
    }
    return {};
}

// texture-tool flipbook: the arguments after the mode.
int MakeFlipbookFiles(std::span<char* const> arguments) {
    if (arguments.size() < 2 || arguments.size() % 2 != 0) {
        std::fputs(kUsage.data(), stderr);
        return 2;
    }
    texturetool::FlipbookSettings settings;
    bool preview = false;
    const std::string_view kind = arguments[0];
    if (kind == "fire") {
        settings.kind = texturetool::FlipbookKind::Fire;
    } else if (kind == "smoke") {
        settings.kind = texturetool::FlipbookKind::Smoke;
    } else if (kind == "explosion") {
        settings.kind = texturetool::FlipbookKind::Explosion;
    } else {
        std::fputs(kUsage.data(), stderr);
        return 2;
    }
    const std::string output = arguments[1];
    for (std::size_t i = 2; i + 1 < arguments.size(); i += 2) {
        const std::string_view option = arguments[i];
        const auto value = static_cast<std::uint32_t>(std::stoul(arguments[i + 1]));
        if (option == "--frames") {
            settings.frames = value;
        } else if (option == "--size") {
            settings.size = value;
        } else if (option == "--grid") {
            settings.grid = value;
        } else if (option == "--seed") {
            settings.seed = value;
        } else if (option == "--preview") {
            preview = value != 0;
        } else {
            std::fputs(kUsage.data(), stderr);
            return 2;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    const core::Result<texturetool::Flipbook> flipbook = texturetool::MakeFlipbook(settings);
    if (!flipbook) {
        std::fputs(std::format("{}\n", core::Format(flipbook.error())).c_str(), stderr);
        return 1;
    }
    for (const auto& [suffix, image, use] : {std::tuple{".ktx2", &flipbook->color, TextureUse::Color},
                                             std::tuple{"_lp.ktx2", &flipbook->lightPositive, TextureUse::Data},
                                             std::tuple{"_ln.ktx2", &flipbook->lightNegative, TextureUse::Data}}) {
        const std::filesystem::path path = output + suffix;
        const core::Result<void> written =
            resources::EncodeKtx2(resources::GenerateMips(*image), use)
                .and_then([&path](const std::vector<std::byte>& bytes) { return Write(path, bytes); });
        if (!written) {
            std::fputs(std::format("{}\n", core::Format(written.error())).c_str(), stderr);
            return 1;
        }
        if (preview) {
            const std::string png = output + std::string(suffix).replace(std::string(suffix).find(".ktx2"), 5, ".png");
            const auto width = static_cast<int>(image->width);
            if (stbi_write_png(png.c_str(), width, static_cast<int>(image->height), 4, image->pixels.data(),
                               width * 4) == 0) {
                std::fputs(std::format("Cannot write '{}'\n", png).c_str(), stderr);
                return 1;
            }
        }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::fputs(std::format("{}: {} frames, \"columns\": {}, \"rows\": {}, in {:.1f} s\n", output, settings.frames,
                           flipbook->columns, flipbook->rows, elapsed.count())
                   .c_str(),
               stdout);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::span<char* const> arguments(argv, static_cast<std::size_t>(argc));
    if (argc > 1 && std::string_view(arguments[1]) == "flipbook") {
        return MakeFlipbookFiles(arguments.subspan(2));
    }
    const bool comparing = argc > 1 && std::string_view(arguments[1]) == "--compare";
    if (comparing) {
        arguments = arguments.subspan(1);
        --argc;
    }
    const std::string_view mode = (argc > 1) ? arguments[1] : "";
    const bool packing = mode == "metallic-roughness";
    const bool known = packing || mode == "color" || mode == "normal" || mode == "data";
    if (!known || argc < 4 || argc > (packing ? 5 : 4)) {
        std::fputs(kUsage.data(), stderr);
        return 2;
    }
    const std::span<char* const> inputs = arguments.subspan(2, static_cast<std::size_t>(argc) - 3);
    const std::filesystem::path output = arguments.back();
    const TextureUse use = (mode == "color")    ? TextureUse::Color
                           : (mode == "normal") ? TextureUse::Normal
                                                : TextureUse::Data;

    if (comparing) {
        const core::Result<std::string> report =
            Load(inputs, packing, use).and_then([&output, use](const ImageData& image) {
                return Compare(image, output, use);
            });
        if (!report) {
            std::fputs(std::format("{}\n", core::Format(report.error())).c_str(), stderr);
            return 1;
        }
        std::fputs(std::format("{}\n", *report).c_str(), stdout);
        return 0;
    }

    const auto start = std::chrono::steady_clock::now();
    const core::Result<std::vector<std::byte>> encoded =
        Load(inputs, packing, use).and_then([use](const ImageData& image) {
            return resources::EncodeKtx2(image, use);
        });
    const core::Result<void> written =
        encoded.and_then([&output](const std::vector<std::byte>& bytes) { return Write(output, bytes); });
    if (!written) {
        std::fputs(std::format("{}\n", core::Format(written.error())).c_str(), stderr);
        return 1;
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::fputs(
        std::format("{}: {} KiB in {:.1f} s\n", output.string(), encoded->size() / 1024, elapsed.count()).c_str(),
        stdout);
    return 0;
}
