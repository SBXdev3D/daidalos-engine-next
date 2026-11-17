// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace editor {

/// The editor's undo and redo (ADR-0055): the level as it was before each edit. An edit opens when something
/// first changes and closes when the mouse lets go of what changed it, so dragging a gizmo or a slider, or
/// typing a name, is one step. Undoing puts the level back and scene::LevelInstance::Sync makes the scene follow.
/// Levels are small (the factory is about a hundred entities), so whole copies cost less than commands per field
/// would, and nothing an edit does can be forgotten by its undo.
class History {
public:
    /// The current state's number, which a save records to know whether the level has changed since.
    using Revision = std::uint64_t;

    /// Before a change: opens an edit with the level as it is, unless one is open already.
    void Begin(const daidalos::scene::Level& current, std::string_view label);
    [[nodiscard]] bool IsOpen() const { return m_open.has_value(); }
    /// Closes the open edit. It becomes a step if the level changed, and clears what could be redone.
    void Commit(const daidalos::scene::Level& current);

    [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
    /// What the next undo or redo would do, as "Move", "Delete Lamp" and so on.
    [[nodiscard]] const std::string& UndoLabel() const;
    [[nodiscard]] const std::string& RedoLabel() const;
    /// The level to go back to, and the current one kept for redo. Commits the open edit first.
    [[nodiscard]] std::optional<daidalos::scene::Level> Undo(const daidalos::scene::Level& current);
    [[nodiscard]] std::optional<daidalos::scene::Level> Redo(const daidalos::scene::Level& current);

    /// Forgets every step: a level was opened.
    void Clear();
    [[nodiscard]] Revision Current() const { return m_revision; }
    [[nodiscard]] std::size_t Steps() const { return m_undo.size(); }

    /// Steps beyond this are forgotten, oldest first.
    static constexpr std::size_t kMaxSteps = 200;

private:
    struct Step {
        std::string label;
        daidalos::scene::Level level;
        Revision revision = 0;
    };

    std::optional<Step> m_open;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    Revision m_revision = 0;
    Revision m_nextRevision = 1;
};

} // namespace editor
