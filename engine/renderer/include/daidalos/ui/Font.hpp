// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/resources/FontData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <string>

namespace daidalos::ui {

/// A baked font and its atlas on the GPU, which whoever created it destroys, such as
/// scene::AssetLibrary::LoadFont.
struct Font {
    resources::FontData data;
    rhi::TextureHandle atlas;
};

/// Uploads the distance field atlas of a font. Not allowed between rhi::Device::BeginFrame and EndFrame.
[[nodiscard]] core::Result<rhi::TextureHandle> CreateFontAtlas(rhi::Device& device, const resources::FontData& font,
                                                               const std::string& name);

} // namespace daidalos::ui
