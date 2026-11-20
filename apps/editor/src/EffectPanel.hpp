// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/EffectDocument.hpp>

#include <glm/vec4.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

/// The effect panel (ADR-0114): a particle effect's or a ribbon's file, its values as fields, its curves as curves,
/// written as they change, so every emitter of the level shows them at once (the game's effects reload when their
/// file changes, ADR-0053). Its edits are steps of the editor's history; Revert puts back the file as it was opened.
class EffectPanel {
public:
    /// Opens a `.effect.json` or `.ribbon.json` file under the asset root.
    void Open(const std::filesystem::path& assetRoot, std::string path);
    void Close();
    [[nodiscard]] bool IsOpen() const { return !m_path.empty(); }
    [[nodiscard]] const std::string& Path() const { return m_path; }
    /// The file as last written: what the history keeps.
    [[nodiscard]] const std::string& Text() const { return m_text; }
    /// Puts text back, as undo or redo does, and writes it.
    void SetText(const std::filesystem::path& assetRoot, const std::string& text);
    void Draw(const std::filesystem::path& assetRoot);
    [[nodiscard]] const std::string& Status() const { return m_status; }

private:
    /// Writes the document if the game's reader takes it; otherwise keeps the reader's complaint to show.
    void Commit(const std::filesystem::path& assetRoot);
    void DrawParticles();
    void DrawRibbon();
    bool Number(const char* label, const char* key, float fallback, float speed, float low, float high,
                const char* format = "%.3f");
    bool Whole(const char* label, const char* key, int fallback, int low, int high);
    bool Flag(const char* label, const char* key, bool fallback);
    bool Range(const char* label, const char* key, float fallback, float speed, float low, float high);
    bool Vector(const char* label, const char* key, const glm::vec3& fallback, float speed);
    bool Choice(const char* label, const char* key, const std::vector<const char*>& choices);
    bool Text(const char* label, const char* key);
    bool CurveField(const char* label, const char* key, float fallback);
    bool ColorsField(const char* label, const char* key);

    std::string m_path;
    bool m_ribbon = false;
    daidalos::scene::EffectDocument m_document;
    std::string m_text;
    std::string m_opened;
    /// Written since it was opened or reverted.
    bool m_edited = false;
    /// Shown in front the frame after it opens.
    bool m_focus = false;
    std::string m_error;
    std::string m_status;
    /// The color key chosen in each gradient, by key.
    std::optional<std::size_t> m_colorKey;
    /// A curve key being dragged.
    std::optional<std::pair<std::string, std::size_t>> m_dragging;
};

/// Curve editing, apart from the panel so that it can be tested: the keys sorted by time, a key added between
/// its neighbours, a key moved within its neighbours.
[[nodiscard]] std::vector<std::pair<float, float>> SortedKeys(std::vector<std::pair<float, float>> keys);
/// The value of a curve at a time, as the GPU blends it: linearly between keys, held before and after.
[[nodiscard]] float CurveAt(const std::vector<std::pair<float, float>>& keys, float time);

} // namespace editor
