// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Sequence.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor {

/// The sequence panel (ADR-0079): a trigger's .sequence.json as a timeline of cues to add, move, change and
/// delete, played in the editor's scene, and saved. Its edits are not steps of the level's undo history;
/// Revert reads the file again.
class SequencePanel {
public:
    /// What the editor gives the panel each frame.
    struct Context {
        const daidalos::scene::Level* level = nullptr;
        std::filesystem::path assetRoot;
        /// Where the editor's camera is and looks, for camera keys set from the view.
        glm::vec3 cameraPosition{0.0f};
        glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
    };
    /// What the panel asks of the editor.
    enum class Request { None, Play, Reset };

    /// Opens a sequence file under the asset root, or starts it empty if it does not exist yet.
    void Open(const std::filesystem::path& assetRoot, std::string path);
    [[nodiscard]] bool IsOpen() const { return !m_path.empty(); }
    [[nodiscard]] const std::string& Path() const { return m_path; }
    [[nodiscard]] const daidalos::scene::Sequence& Sequence() const { return m_sequence; }
    [[nodiscard]] bool Dirty() const { return m_sequence != m_saved; }
    bool Save(const std::filesystem::path& assetRoot);
    void Close();

    /// The window. Returns what it asks the editor to do: play the sequence as it is, or put the scene back.
    [[nodiscard]] Request Draw(const Context& context);
    /// Seconds since Play, for the playhead; the editor sets it while a preview runs.
    void SetPlayhead(std::optional<float> seconds) { m_playhead = seconds; }
    /// The selected cue, if any: the viewport draws a camera's path or where a move goes.
    [[nodiscard]] const daidalos::scene::Cue* Selected() const;
    [[nodiscard]] const std::string& Status() const { return m_status; }

private:
    void DrawTimeline();
    /// The fields of the selected cue. Returns true when one changed.
    bool DrawCue(daidalos::scene::Cue& cue, const Context& context);
    bool EntityField(const char* label, std::string& name, const Context& context);
    /// Keeps the cues in time order and the selection on the same cue.
    void Sort();

    std::string m_path;
    daidalos::scene::Sequence m_sequence;
    daidalos::scene::Sequence m_saved;
    std::optional<std::size_t> m_selected;
    std::optional<float> m_playhead;
    /// Seconds per timeline pixel are chosen to fit; this is where new cues go.
    float m_cursor = 0.0f;
    int m_newKind = 0;
    std::string m_status;
};

} // namespace editor
