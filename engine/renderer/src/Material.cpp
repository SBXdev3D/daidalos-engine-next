// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/Material.hpp>

#include <utility>

namespace daidalos::renderer {

namespace {

rhi::Format ToRhiFormat(resources::PixelFormat format) {
    switch (format) {
    case resources::PixelFormat::Rgba8Unorm:
        return rhi::Format::Rgba8Unorm;
    case resources::PixelFormat::Rgba8Srgb:
        return rhi::Format::Rgba8Srgb;
    case resources::PixelFormat::Bc5Unorm:
        return rhi::Format::Bc5Unorm;
    case resources::PixelFormat::Bc7Unorm:
        return rhi::Format::Bc7Unorm;
    case resources::PixelFormat::Bc7Srgb:
        return rhi::Format::Bc7Srgb;
    }
    return rhi::Format::Undefined;
}

} // namespace

core::Result<rhi::TextureHandle> CreateTexture(rhi::Device& device, const resources::ImageData& image,
                                               const std::string& name) {
    core::Result<rhi::TextureHandle> texture =
        device.CreateTexture({.extent = {image.width, image.height},
                              .format = ToRhiFormat(image.format),
                              .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                              .mipLevels = image.mipLevels,
                              .name = name});
    if (!texture) {
        return texture;
    }
    if (core::Result<void> uploaded = device.UploadTexture(*texture, image.pixels); !uploaded) {
        device.DestroyTexture(*texture);
        return tl::unexpected(std::move(uploaded.error()));
    }
    return texture;
}

} // namespace daidalos::renderer
