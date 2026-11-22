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
//     texture-tool relief <roughness.png> <metallic.png|-> <height.png> <out.ktx2> [--depth N] [--albedo d.png]
//
// packs the same, with the height in alpha and the occlusion baked from it in red (ADR-0064): at each texel,
// how much of the sky above it the relief around leaves open, over 16 directions, the height spanning N
// texels (8 by default). With an albedo, it prints how much the albedo already darkens where the relief
// occludes, from 0 (not at all) to 1 (as much), so that a material does not darken its cavities twice.
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
#include <cstdlib>
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
    "       texture-tool relief <roughness> <metallic|-> <height> <file.ktx2> [--depth N] [--albedo <albedo>]\n"
    "       texture-tool flipbook fire|smoke|explosion <out> [--frames N] [--size N] [--grid N] [--seed N]\n"
    "                    [--preview 1]\n";

// The image without its mip levels larger than maxSize on either side: a smaller texture for a small prop
// (ADR-0082). Its levels were averaged from the full image, so nothing is resampled again.
ImageData LimitSize(ImageData image, std::uint32_t maxSize) {
    while ((image.width > maxSize || image.height > maxSize) && image.mipLevels > 1) {
        const std::size_t top = std::size_t{image.width} * image.height * 4;
        image.pixels.erase(image.pixels.begin(), image.pixels.begin() + static_cast<std::ptrdiff_t>(top));
        image.width = std::max(image.width / 2, 1U);
        image.height = std::max(image.height / 2, 1U);
        --image.mipLevels;
    }
    return image;
}

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

// The first level of an image, alone.
ImageData FirstLevel(const ImageData& image) {
    ImageData level;
    level.width = image.width;
    level.height = image.height;
    level.format = image.format;
    level.pixels.assign(image.pixels.begin(), image.pixels.begin() + static_cast<std::ptrdiff_t>(
                                                                         std::size_t{image.width} * image.height * 4));
    return level;
}

// The occlusion of a height map (ADR-0064), from 0 (closed) to 1 (open): at each texel, the share of the
// sky above it that the relief around leaves open, over 16 directions, each to the highest horizon within
// four depths. The map tiles. depth is the texels the height spans from 0 to 1.
std::vector<float> BakeOcclusion(const ImageData& height, float depth) {
    const int width = static_cast<int>(height.width);
    const int rows = static_cast<int>(height.height);
    const auto at = [&](int x, int y) {
        x = ((x % width) + width) % width;
        y = ((y % rows) + rows) % rows;
        return static_cast<float>(height.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                                 static_cast<std::size_t>(x)) *
                                                4]) /
               255.0f;
    };
    constexpr int kDirections = 16;
    constexpr int kSteps = 12;
    const float reach = depth * 4.0f;
    std::vector<float> occlusion(static_cast<std::size_t>(width) * static_cast<std::size_t>(rows));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < width; ++x) {
            const float here = at(x, y);
            float open = 0.0f;
            for (int d = 0; d < kDirections; ++d) {
                const float angle = (static_cast<float>(d) + 0.5f) * 2.0f * std::numbers::pi_v<float> / kDirections;
                float steepest = 0.0f;
                for (int step = 1; step <= kSteps; ++step) {
                    const float distance = reach * std::pow(static_cast<float>(step) / kSteps, 1.5f);
                    const float rise = (at(x + static_cast<int>(std::lround(std::cos(angle) * distance)),
                                           y + static_cast<int>(std::lround(std::sin(angle) * distance))) -
                                        here) *
                                       depth;
                    steepest = std::max(steepest, rise / std::max(distance, 1.0f));
                }
                // The sky above the horizon, weighed by the cosine: 1 - sin² of its elevation.
                const float sine = steepest / std::sqrt(1.0f + steepest * steepest);
                open += 1.0f - sine * sine;
            }
            occlusion[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                open / kDirections;
        }
    }
    return occlusion;
}

// Roughness in green, metalness in blue, the occlusion baked from the height in red, the height in alpha.
core::Result<ImageData> PackRelief(const ImageData& roughness, const ImageData* metallic, const ImageData& height,
                                   float depth, const std::vector<float>& occlusion) {
    if (height.width != roughness.width || height.height != roughness.height ||
        (metallic != nullptr && (metallic->width != roughness.width || metallic->height != roughness.height))) {
        return core::MakeError(core::ErrorCode::InvalidArgument, "texture-tool",
                               "The roughness, metalness and height maps differ in size");
    }
    (void)depth;
    ImageData packed = FirstLevel(roughness);
    for (std::size_t texel = 0; texel < occlusion.size(); ++texel) {
        const std::size_t at = texel * 4;
        packed.pixels[at] = static_cast<std::byte>(std::lround(std::clamp(occlusion[texel], 0.0f, 1.0f) * 255.0f));
        packed.pixels[at + 1] = roughness.pixels[at];
        packed.pixels[at + 2] = (metallic != nullptr) ? metallic->pixels[at] : std::byte{255};
        packed.pixels[at + 3] = height.pixels[at];
    }
    return resources::GenerateMips(std::move(packed));
}

// How much the albedo darkens where the relief occludes: the correlation of its luminance with the
// occlusion, from 0 (none, or the other way) to 1.
double CavityCorrelation(const ImageData& albedo, const std::vector<float>& occlusion) {
    double sumA = 0.0;
    double sumO = 0.0;
    double sumAA = 0.0;
    double sumOO = 0.0;
    double sumAO = 0.0;
    const double count = static_cast<double>(occlusion.size());
    for (std::size_t texel = 0; texel < occlusion.size(); ++texel) {
        const std::size_t at = texel * 4;
        const double a =
            (0.2126 * static_cast<double>(albedo.pixels[at]) + 0.7152 * static_cast<double>(albedo.pixels[at + 1]) +
             0.0722 * static_cast<double>(albedo.pixels[at + 2])) /
            255.0;
        const double o = occlusion[texel];
        sumA += a;
        sumO += o;
        sumAA += a * a;
        sumOO += o * o;
        sumAO += a * o;
    }
    const double covariance = sumAO / count - (sumA / count) * (sumO / count);
    const double spread = std::sqrt(std::max(
        (sumAA / count - (sumA / count) * (sumA / count)) * (sumOO / count - (sumO / count) * (sumO / count)), 1e-12));
    return std::clamp(covariance / spread, 0.0, 1.0);
}

core::Result<void> Write(const std::filesystem::path& path, std::span<const std::byte> bytes);

int MakeRelief(std::span<char* const> arguments) {
    if (arguments.size() < 4) {
        std::fputs(kUsage.data(), stderr);
        return 2;
    }
    float depth = 8.0f;
    const char* albedoPath = nullptr;
    for (std::size_t index = 4; index + 1 < arguments.size(); index += 2) {
        const std::string_view option = arguments[index];
        if (option == "--depth") {
            depth = std::max(std::stof(arguments[index + 1]), 0.1f);
        } else if (option == "--albedo") {
            albedoPath = arguments[index + 1];
        } else {
            std::fputs(kUsage.data(), stderr);
            return 2;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    const std::string_view metallicPath = arguments[1];
    const core::Result<ImageData> roughness = resources::LoadImageFile(arguments[0], TextureUse::Data);
    const core::Result<ImageData> height = resources::LoadImageFile(arguments[2], TextureUse::Data);
    std::optional<core::Result<ImageData>> metallic;
    if (metallicPath != "-") {
        metallic = resources::LoadImageFile(arguments[1], TextureUse::Data);
    }
    if (!roughness || !height || (metallic.has_value() && !metallic->has_value())) {
        const core::Error& error = !roughness ? roughness.error() : (!height ? height.error() : metallic->error());
        std::fputs(std::format("{}\n", core::Format(error)).c_str(), stderr);
        return 1;
    }
    const std::vector<float> occlusion = BakeOcclusion(*height, depth);
    const core::Result<void> written =
        PackRelief(*roughness, metallic.has_value() ? &**metallic : nullptr, *height, depth, occlusion)
            .and_then([](const ImageData& image) { return resources::EncodeKtx2(image, TextureUse::Data); })
            .and_then([&](const std::vector<std::byte>& bytes) { return Write(arguments[3], bytes); });
    if (!written) {
        std::fputs(std::format("{}\n", core::Format(written.error())).c_str(), stderr);
        return 1;
    }
    double mean = 0.0;
    for (const float value : occlusion) {
        mean += value / static_cast<double>(occlusion.size());
    }
    std::string report = std::format("{}: occlusion {:.3f} on average", arguments[3], mean);
    if (albedoPath != nullptr) {
        const core::Result<ImageData> albedo = resources::LoadImageFile(albedoPath, TextureUse::Color);
        if (albedo && albedo->width == height->width && albedo->height == height->height) {
            report +=
                std::format(", cavities already dark in the albedo {:.2f}", CavityCorrelation(*albedo, occlusion));
        }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::fputs(std::format("{}, in {:.1f} s\n", report, elapsed.count()).c_str(), stdout);
    return 0;
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
    if (argc > 1 && std::string_view(arguments[1]) == "relief") {
        return MakeRelief(arguments.subspan(2));
    }
    const bool comparing = argc > 1 && std::string_view(arguments[1]) == "--compare";
    if (comparing) {
        arguments = arguments.subspan(1);
        --argc;
    }
    std::uint32_t maxSize = 0;
    std::vector<char*> kept;
    if (argc > 2 && std::string_view(arguments[1]) == "--max-size") {
        maxSize = static_cast<std::uint32_t>(std::max(std::atoi(arguments[2]), 1));
        kept.assign(arguments.begin(), arguments.end());
        kept.erase(kept.begin() + 1, kept.begin() + 3);
        arguments = std::span<char* const>(kept);
        argc -= 2;
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
        Load(inputs, packing, use).and_then([use, maxSize](const ImageData& image) {
            return resources::EncodeKtx2(maxSize > 0 ? LimitSize(image, maxSize) : image, use);
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
