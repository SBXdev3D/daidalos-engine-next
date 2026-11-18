// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/WorkerPool.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Import.hpp"

namespace editor {

/// The asset browser (ADR-0055): the folders under assets/, and in the open one the models, skies, effects,
/// prefabs, decals and levels, as tiles. Models show a clay thumbnail, skies their front face and decals their image,
/// made on workers and shown once ready. A tile is placed by a double click or by dragging it into the viewport; files
/// dropped on the window from the system are imported into the open folder.
class AssetBrowser {
public:
    AssetBrowser(std::filesystem::path assetRoot, daidalos::rhi::Device& device, daidalos::core::WorkerPool& workers);
    AssetBrowser(const AssetBrowser&) = delete;
    AssetBrowser& operator=(const AssetBrowser&) = delete;
    ~AssetBrowser();

    /// Opens a folder under the asset root, and lists it again.
    void Open(std::string folder);
    [[nodiscard]] const std::string& Folder() const { return m_folder; }

    /// Draws the panel. Returns the asset double-clicked, to be placed or opened.
    [[nodiscard]] std::optional<AssetEntry> Draw();
    /// The asset being dragged out of the panel, if any: placed where it is let go.
    [[nodiscard]] const std::optional<AssetEntry>& Dragged() const { return m_dragged; }
    void EndDrag() { m_dragged.reset(); }

    /// Copies a model or a sky from outside the assets into the open folder (Import.hpp). Returns what it became.
    [[nodiscard]] daidalos::core::Result<AssetEntry> Import(const std::filesystem::path& file);

private:
    struct Thumbnail {
        enum class State { Waiting, Ready, Failed } state = State::Waiting;
        daidalos::rhi::TextureHandle texture;
    };

    /// The thumbnail of an asset, which a worker starts drawing the first time it is asked for.
    const Thumbnail* ThumbnailOf(const AssetEntry& entry);
    void DrawTile(const AssetEntry& entry, std::optional<AssetEntry>& chosen);

    std::filesystem::path m_assetRoot;
    daidalos::rhi::Device* m_device;
    daidalos::core::WorkerPool* m_workers;
    std::string m_folder;
    std::vector<AssetEntry> m_entries;
    std::string m_filter;
    std::string m_importPath;
    std::string m_message;
    std::optional<AssetEntry> m_dragged;
    std::unordered_map<std::string, Thumbnail> m_thumbnails;
    std::size_t m_drawing = 0;
    /// Outlives the browser in the jobs still running, which then deliver nothing.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace editor
