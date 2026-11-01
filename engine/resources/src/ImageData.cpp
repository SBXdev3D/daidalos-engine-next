// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/resources/AssetPath.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <ktx.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <optional>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

// Numeric values of VkFormat, which this module does not depend on.
constexpr std::uint32_t kVkRgba8Unorm = 37;
constexpr std::uint32_t kVkRgba8Srgb = 43;
constexpr std::uint32_t kVkBc5Unorm = 141;
constexpr std::uint32_t kVkBc7Unorm = 145;
constexpr std::uint32_t kVkBc7Srgb = 146;

struct KtxDeleter {
    void operator()(ktxTexture2* texture) const { ktxTexture_Destroy(ktxTexture(texture)); }
};

float SrgbToLinear(float value) {
    return (value <= 0.04045f) ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value) {
    return (value <= 0.0031308f) ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::array<float, 256> MakeSrgbTable() {
    std::array<float, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        table[index] = SrgbToLinear(static_cast<float>(index) / 255.0f);
    }
    return table;
}

std::optional<PixelFormat> ToPixelFormat(std::uint32_t vkFormat) {
    switch (vkFormat) {
    case kVkRgba8Unorm:
        return PixelFormat::Rgba8Unorm;
    case kVkRgba8Srgb:
        return PixelFormat::Rgba8Srgb;
    case kVkBc5Unorm:
        return PixelFormat::Bc5Unorm;
    case kVkBc7Unorm:
        return PixelFormat::Bc7Unorm;
    case kVkBc7Srgb:
        return PixelFormat::Bc7Srgb;
    default:
        return std::nullopt;
    }
}

bool IsKtx2(std::span<const std::byte> bytes) {
    static constexpr std::array<unsigned char, 12> kIdentifier{0xAB, 'K',  'T',  'X',  ' ',  '2',
                                                               '0',  0xBB, '\r', '\n', 0x1A, '\n'};
    return bytes.size() >= kIdentifier.size() && std::memcmp(bytes.data(), kIdentifier.data(), kIdentifier.size()) == 0;
}

core::Result<ImageData> DecodeKtx2(std::span<const std::byte> bytes, TextureUse use, const std::string& name) {
    ktxTexture2* created = nullptr;
    const ktx_error_code_e opened =
        ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()), bytes.size(),
                                     KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &created);
    if (opened != KTX_SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, std::string(kModule),
                               std::format("KTX2 image '{}': {}", name, ktxErrorString(opened)));
    }
    const std::unique_ptr<ktxTexture2, KtxDeleter> texture(created);

    if (texture->numDimensions != 2 || texture->isArray || texture->isCubemap) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                               std::format("KTX2 image '{}' is not a plain 2D texture", name));
    }
    if (ktxTexture2_NeedsTranscoding(texture.get())) {
        const ktx_transcode_fmt_e target = (use == TextureUse::Normal) ? KTX_TTF_BC5_RG : KTX_TTF_BC7_RGBA;
        const ktx_error_code_e transcoded = ktxTexture2_TranscodeBasis(texture.get(), target, 0);
        if (transcoded != KTX_SUCCESS) {
            return core::MakeError(core::ErrorCode::ParseError, std::string(kModule),
                                   std::format("Transcoding KTX2 image '{}': {}", name, ktxErrorString(transcoded)));
        }
    }
    const std::optional<PixelFormat> format = ToPixelFormat(texture->vkFormat);
    if (!format.has_value()) {
        return core::MakeError(core::ErrorCode::NotSupported, std::string(kModule),
                               std::format("KTX2 image '{}' has unsupported format {}", name, texture->vkFormat));
    }

    ImageData image;
    image.width = texture->baseWidth;
    image.height = texture->baseHeight;
    image.mipLevels = texture->numLevels;
    image.format = *format;
    const ktx_uint8_t* data = ktxTexture_GetData(ktxTexture(texture.get()));
    for (std::uint32_t level = 0; level < texture->numLevels; ++level) {
        ktx_size_t offset = 0;
        const ktx_error_code_e located = ktxTexture_GetImageOffset(ktxTexture(texture.get()), level, 0, 0, &offset);
        if (located != KTX_SUCCESS) {
            return core::MakeError(core::ErrorCode::ParseError, std::string(kModule),
                                   std::format("KTX2 image '{}': {}", name, ktxErrorString(located)));
        }
        const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(texture.get()), level);
        const auto* first = reinterpret_cast<const std::byte*>(data + offset);
        image.pixels.insert(image.pixels.end(), first, first + size);
    }
    return image;
}

core::Result<ImageData> DecodeWithStb(std::span<const std::byte> bytes, TextureUse use, const std::string& name) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &width,
                              &height, &channels, 4),
        &stbi_image_free);
    if (decoded == nullptr) {
        return core::MakeError(core::ErrorCode::ParseError, std::string(kModule),
                               std::format("Image '{}': {}", name, stbi_failure_reason()));
    }

    ImageData image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.format = (use == TextureUse::Color) ? PixelFormat::Rgba8Srgb : PixelFormat::Rgba8Unorm;
    const auto* first = reinterpret_cast<const std::byte*>(decoded.get());
    image.pixels.assign(first, first + std::size_t{image.width} * image.height * 4);
    return GenerateMips(std::move(image));
}

} // namespace

ImageData GenerateMips(ImageData image) {
    DAI_PROFILE_SCOPE("resources::GenerateMips");
    DAI_ASSERT(image.format == PixelFormat::Rgba8Unorm || image.format == PixelFormat::Rgba8Srgb,
               "Mip levels can only be generated for RGBA8 images");
    DAI_ASSERT(image.mipLevels == 1, "The image already has mip levels");

    static const std::array<float, 256> kSrgbTable = MakeSrgbTable();
    const bool srgb = image.format == PixelFormat::Rgba8Srgb;
    const auto levels = static_cast<std::uint32_t>(std::bit_width(std::max(image.width, image.height)));

    std::size_t total = 0;
    for (std::uint32_t level = 0; level < levels; ++level) {
        total += std::size_t{std::max(image.width >> level, 1U)} * std::max(image.height >> level, 1U) * 4;
    }
    image.pixels.resize(total);

    auto* pixels = reinterpret_cast<std::uint8_t*>(image.pixels.data());
    const std::uint8_t* source = pixels;
    std::uint32_t sourceWidth = image.width;
    std::uint32_t sourceHeight = image.height;
    for (std::uint32_t level = 1; level < levels; ++level) {
        std::uint8_t* destination = const_cast<std::uint8_t*>(source) + std::size_t{sourceWidth} * sourceHeight * 4;
        const std::uint32_t width = std::max(sourceWidth / 2, 1U);
        const std::uint32_t height = std::max(sourceHeight / 2, 1U);
        for (std::uint32_t y = 0; y < height; ++y) {
            // An odd row or column at the end of the source is left out, as rounding the size down implies.
            const std::uint32_t top = (sourceHeight == 1) ? 0 : y * 2;
            const std::uint32_t bottom = (sourceHeight == 1) ? 0 : y * 2 + 1;
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint32_t left = (sourceWidth == 1) ? 0 : x * 2;
                const std::uint32_t right = (sourceWidth == 1) ? 0 : x * 2 + 1;
                const std::array<const std::uint8_t*, 4> corners{
                    source + (std::size_t{top} * sourceWidth + left) * 4,
                    source + (std::size_t{top} * sourceWidth + right) * 4,
                    source + (std::size_t{bottom} * sourceWidth + left) * 4,
                    source + (std::size_t{bottom} * sourceWidth + right) * 4,
                };
                std::uint8_t* out = destination + (std::size_t{y} * width + x) * 4;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    float sum = 0.0f;
                    const bool decode = srgb && channel < 3;
                    for (const std::uint8_t* corner : corners) {
                        sum += decode ? kSrgbTable[corner[channel]] : static_cast<float>(corner[channel]) / 255.0f;
                    }
                    const float average = sum / 4.0f;
                    const float encoded = decode ? LinearToSrgb(average) : average;
                    out[channel] = static_cast<std::uint8_t>(std::clamp(encoded, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
        source = destination;
        sourceWidth = width;
        sourceHeight = height;
    }
    image.mipLevels = levels;
    return image;
}

core::Result<ImageData> DecodeImage(std::span<const std::byte> bytes, TextureUse use, const std::string& name) {
    DAI_PROFILE_SCOPE("resources::DecodeImage");
    return IsKtx2(bytes) ? DecodeKtx2(bytes, use, name) : DecodeWithStb(bytes, use, name);
}

core::Result<ImageData> LoadImageFile(const std::filesystem::path& path, TextureUse use) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return core::MakeError(core::ErrorCode::NotFound, std::string(kModule),
                               std::format("Cannot open image file '{}'", ToUtf8(path)));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, std::string(kModule),
                               std::format("Cannot read image file '{}'", ToUtf8(path)));
    }
    return DecodeImage(bytes, use, ToUtf8(path));
}

} // namespace daidalos::resources
