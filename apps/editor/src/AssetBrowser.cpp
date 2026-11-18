// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "AssetBrowser.hpp"

#include <daidalos/debugui/DebugUi.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/resources/ImageData.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <span>
#include <utility>

#include "Inspector.hpp"
#include "Thumbnail.hpp"

using namespace daidalos;

namespace editor {

namespace {

constexpr std::string_view kModule = "editor";
constexpr float kTile = 96.0f;            // Interface pixels.
constexpr std::size_t kDrawingAtOnce = 2; // Thumbnails drawn by workers at the same time.

const char* KindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Folder:
        return "Folder";
    case AssetKind::Model:
        return "Model";
    case AssetKind::Sky:
        return "Sky";
    case AssetKind::Effect:
        return "Particles";
    case AssetKind::Ribbon:
        return "Ribbon";
    case AssetKind::Level:
        return "Level";
    case AssetKind::Prefab:
        return "Prefab";
    case AssetKind::Decal:
        return "Decal";
    case AssetKind::Sequence:
        return "Sequence";
    case AssetKind::Other:
        break;
    }
    return "File";
}

ImU32 KindColor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Folder:
        return IM_COL32(200, 170, 90, 255);
    case AssetKind::Model:
        return IM_COL32(170, 170, 170, 255);
    case AssetKind::Sky:
        return IM_COL32(120, 170, 230, 255);
    case AssetKind::Effect:
        return IM_COL32(240, 150, 60, 255);
    case AssetKind::Ribbon:
        return IM_COL32(230, 110, 200, 255);
    case AssetKind::Level:
        return IM_COL32(120, 210, 120, 255);
    case AssetKind::Prefab:
        return IM_COL32(90, 200, 210, 255);
    case AssetKind::Decal:
        return IM_COL32(200, 80, 80, 255);
    case AssetKind::Sequence:
        return IM_COL32(230, 230, 120, 255);
    case AssetKind::Other:
        break;
    }
    return IM_COL32(150, 150, 150, 255);
}

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

AssetBrowser::AssetBrowser(std::filesystem::path assetRoot, rhi::Device& device, core::WorkerPool& workers)
    : m_assetRoot(std::move(assetRoot)), m_device(&device), m_workers(&workers) {}

AssetBrowser::~AssetBrowser() {
    *m_alive = false;
    [[maybe_unused]] const core::Result<void> idle = m_device->WaitIdle();
    for (auto& [path, thumbnail] : m_thumbnails) {
        if (!thumbnail.texture.IsNull()) {
            m_device->DestroyTexture(thumbnail.texture);
        }
    }
}

void AssetBrowser::Open(std::string folder) {
    while (!folder.empty() && folder.back() == '/') {
        folder.pop_back();
    }
    m_folder = std::move(folder);
    m_entries = ListFolder(m_assetRoot, m_folder);
}

core::Result<AssetEntry> AssetBrowser::Import(const std::filesystem::path& file) {
    const std::filesystem::path directory = m_assetRoot / std::filesystem::path(m_folder);
    AssetEntry entry;
    if (KindOf(file) == AssetKind::Model) {
        DAI_TRY_ASSIGN(const std::filesystem::path model, ImportModel(file, directory));
        entry = {.kind = AssetKind::Model,
                 .name = model.filename().generic_string(),
                 .path = model.lexically_relative(m_assetRoot).generic_string()};
    } else if (SkyPrefix(file.filename().generic_string())) {
        DAI_TRY_ASSIGN(const std::filesystem::path prefix, ImportSky(file, directory));
        entry = {.kind = AssetKind::Sky,
                 .name = prefix.filename().generic_string(),
                 .path = prefix.lexically_relative(m_assetRoot).generic_string()};
    } else {
        return core::MakeError(core::ErrorCode::InvalidArgument, kModule,
                               std::format("{}: only glTF models (.gltf, .glb) and the faces of skies "
                                           "(name_front.png and the other five) can be imported",
                                           file.filename().generic_string()));
    }
    m_entries = ListFolder(m_assetRoot, m_folder);
    m_message = std::format("Imported {}", entry.path);
    DAI_LOG_INFO(kModule, "{}", m_message);
    return entry;
}

const AssetBrowser::Thumbnail* AssetBrowser::ThumbnailOf(const AssetEntry& entry) {
    if (entry.kind != AssetKind::Model && entry.kind != AssetKind::Sky && entry.kind != AssetKind::Decal) {
        return nullptr;
    }
    if (const auto found = m_thumbnails.find(entry.path); found != m_thumbnails.end()) {
        return &found->second;
    }
    if (m_drawing >= kDrawingAtOnce) {
        return nullptr;
    }
    m_thumbnails.emplace(entry.path, Thumbnail{});
    ++m_drawing;
    const std::filesystem::path file = entry.kind == AssetKind::Sky
                                           ? m_assetRoot / std::filesystem::path(entry.path + "_front.png")
                                           : m_assetRoot / std::filesystem::path(entry.path);
    const AssetKind kind = entry.kind;
    m_workers->Submit(
        [file, kind]() -> std::vector<std::uint8_t> {
            if (kind == AssetKind::Sky || kind == AssetKind::Decal) {
                core::Result<resources::ImageData> image = resources::LoadImageFile(file, resources::TextureUse::Color);
                return image ? Shrink(*image, kThumbnailSize) : std::vector<std::uint8_t>{};
            }
            core::Result<resources::ModelData> model = resources::LoadModelFile(file);
            return model ? RenderClay(*model, kThumbnailSize) : std::vector<std::uint8_t>{};
        },
        [this, alive = m_alive, path = entry.path](std::vector<std::uint8_t> pixels) {
            if (!*alive) {
                return;
            }
            --m_drawing;
            Thumbnail& thumbnail = m_thumbnails[path];
            thumbnail.state = Thumbnail::State::Failed;
            if (pixels.empty()) {
                return;
            }
            core::Result<rhi::TextureHandle> texture =
                m_device->CreateTexture({.extent = {kThumbnailSize, kThumbnailSize},
                                         .format = rhi::Format::Rgba8Unorm,
                                         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                                         .name = "Editor thumbnail"});
            if (!texture) {
                DAI_LOG_WARNING(kModule, "{}", core::Format(texture.error()));
                return;
            }
            if (core::Result<void> uploaded = m_device->UploadTexture(*texture, std::as_bytes(std::span(pixels)));
                !uploaded) {
                DAI_LOG_WARNING(kModule, "{}", core::Format(uploaded.error()));
                m_device->DestroyTexture(*texture);
                return;
            }
            thumbnail.texture = *texture;
            thumbnail.state = Thumbnail::State::Ready;
        });
    return &m_thumbnails[entry.path];
}

std::optional<AssetEntry> AssetBrowser::Draw() {
    std::optional<AssetEntry> chosen;
    if (!ImGui::Begin("Assets")) {
        ImGui::End();
        return chosen;
    }
    // The path, each folder of it a button that goes there.
    if (ImGui::SmallButton("assets")) {
        Open({});
    }
    std::string walked;
    for (std::size_t start = 0; start < m_folder.size();) {
        std::size_t end = m_folder.find('/', start);
        if (end == std::string::npos) {
            end = m_folder.size();
        }
        walked = m_folder.substr(0, end);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextDisabled("/");
        ImGui::SameLine(0.0f, 2.0f);
        const std::string label = std::format("{}##{}", m_folder.substr(start, end - start), walked);
        if (ImGui::SmallButton(label.c_str())) {
            Open(walked);
            ImGui::End();
            return chosen;
        }
        start = end + 1;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 150.0f, 80.0f));
    InputString("##search", m_filter);
    if (m_filter.empty() && !ImGui::IsItemActive()) {
        const ImVec2 at = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText({at.x + 6.0f, at.y + 3.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                            "Search");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        m_importPath.clear();
        ImGui::OpenPopup("Import");
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        Open(m_folder);
    }
    if (ImGui::BeginPopup("Import")) {
        ImGui::TextUnformatted("A glTF model, or one face of a sky, to copy into this folder:");
        ImGui::SetNextItemWidth(480.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##import", m_importPath);
        if (ImGui::Button("Import") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            std::string path = m_importPath;
            std::erase(path, '"');
            if (core::Result<AssetEntry> imported = Import(path); !imported) {
                m_message = core::Format(imported.error());
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Files can also be dropped on the window.");
        ImGui::EndPopup();
    }
    if (!m_message.empty()) {
        ImGui::TextDisabled("%s", m_message.c_str());
    }

    ImGui::BeginChild("tiles");
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>(width / (kTile + ImGui::GetStyle().ItemSpacing.x)));
    int column = 0;
    const std::string filter = Lower(m_filter);
    // m_entries changes when a folder is opened from a tile: iterate over a copy.
    const std::vector<AssetEntry> entries = m_entries;
    for (const AssetEntry& entry : entries) {
        if (!filter.empty() && Lower(entry.name).find(filter) == std::string::npos) {
            continue;
        }
        if (column > 0) {
            ImGui::SameLine();
        }
        DrawTile(entry, chosen);
        column = (column + 1) % columns;
    }
    if (entries.empty()) {
        ImGui::TextDisabled("Nothing to place here. Drop glTF models or sky faces on the window to import them.");
    }
    ImGui::EndChild();
    ImGui::End();
    return chosen;
}

void AssetBrowser::DrawTile(const AssetEntry& entry, std::optional<AssetEntry>& chosen) {
    ImGui::PushID(entry.path.c_str());
    ImGui::BeginGroup();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("tile", ImVec2(kTile, kTile));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{corner.x + kTile, corner.y + kTile};
    draw->AddRectFilled(corner, end, hovered ? IM_COL32(60, 60, 66, 255) : IM_COL32(38, 38, 42, 255), 3.0f);
    const Thumbnail* thumbnail = ImGui::IsItemVisible() ? ThumbnailOf(entry) : nullptr;
    if (thumbnail != nullptr && thumbnail->state == Thumbnail::State::Ready) {
        draw->AddImage(static_cast<ImTextureID>(debugui::TextureId(m_device->BindlessIndex(thumbnail->texture))),
                       {corner.x + 4.0f, corner.y + 4.0f}, {end.x - 4.0f, end.y - 4.0f});
    } else {
        // A badge of the kind, until there is a picture.
        const char* name =
            thumbnail != nullptr && thumbnail->state == Thumbnail::State::Waiting ? "..." : KindName(entry.kind);
        const ImVec2 size = ImGui::CalcTextSize(name);
        draw->AddRect({corner.x + 14.0f, corner.y + 14.0f}, {end.x - 14.0f, end.y - 14.0f}, KindColor(entry.kind), 4.0f,
                      0, 2.0f);
        draw->AddText({corner.x + (kTile - size.x) * 0.5f, corner.y + (kTile - size.y) * 0.5f}, KindColor(entry.kind),
                      name);
    }
    draw->AddRectFilled({corner.x, end.y - 3.0f}, end, KindColor(entry.kind), 0.0f);

    if (hovered) {
        ImGui::SetTooltip("%s\n%s", entry.path.c_str(),
                          entry.kind == AssetKind::Folder     ? "Double-click to open"
                          : entry.kind == AssetKind::Level    ? "Double-click to open the level"
                          : entry.kind == AssetKind::Sky      ? "Double-click to use as the sky, or drag into the view"
                          : entry.kind == AssetKind::Decal    ? "Double-click to paint with it, or drag onto a surface"
                          : entry.kind == AssetKind::Sequence ? "Double-click to edit its timeline"
                                                              : "Double-click to place in front of the camera, or drag "
                                                                "into the view");
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (entry.kind == AssetKind::Folder) {
            Open(entry.path);
        } else {
            chosen = entry;
        }
    }
    (void)clicked;
    if (entry.kind != AssetKind::Folder && entry.kind != AssetKind::Level && entry.kind != AssetKind::Sequence &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        m_dragged = entry;
        ImGui::SetDragDropPayload("DAIDALOS_ASSET", entry.path.c_str(), entry.path.size() + 1);
        ImGui::Text("%s %s", KindName(entry.kind), entry.name.c_str());
        ImGui::EndDragDropSource();
    }

    // The name, cut to the tile's width.
    std::string label = entry.name;
    while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > kTile) {
        label.resize(label.size() - 4);
        label += "..";
    }
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndGroup();
    ImGui::PopID();
}

} // namespace editor
