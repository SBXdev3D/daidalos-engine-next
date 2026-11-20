// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/resources/ModelData.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Sequence.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor {

/// The editor's undo and redo (ADR-0055): the state as it was before each edit. An edit opens when something
/// first changes and closes when the mouse lets go of what changed it, so dragging a gizmo or a slider, or
/// typing a name, is one step. Undoing puts the state back and the editor makes the scene follow. States are
/// small (the factory's level is about 30 KB), so whole copies cost less than commands per field would, and
/// nothing an edit does can be forgotten by its undo. State needs ==.
template <class State>
class UndoHistory {
public:
    /// The current state's number, which a save records to know whether the state has changed since.
    using Revision = std::uint64_t;

    /// Before a change: opens an edit with the state as it is, unless one is open already.
    void Begin(const State& current, std::string_view label) {
        if (!m_open) {
            m_open = Step{.label = std::string(label), .state = current, .revision = m_revision};
        }
    }
    [[nodiscard]] bool IsOpen() const { return m_open.has_value(); }
    /// Closes the open edit. It becomes a step if the state changed, and clears what could be redone.
    void Commit(const State& current) {
        if (!m_open) {
            return;
        }
        Step step = std::move(*m_open);
        m_open.reset();
        if (step.state == current) {
            return;
        }
        m_undo.push_back(std::move(step));
        if (m_undo.size() > kMaxSteps) {
            m_undo.erase(m_undo.begin());
        }
        m_redo.clear();
        m_revision = m_nextRevision++;
    }

    [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
    /// What the next undo or redo would do, as "Move", "Delete Lamp" and so on.
    [[nodiscard]] const std::string& UndoLabel() const { return m_undo.empty() ? Nothing() : m_undo.back().label; }
    [[nodiscard]] const std::string& RedoLabel() const { return m_redo.empty() ? Nothing() : m_redo.back().label; }
    /// The labels of the steps that can be undone, oldest first, for a history panel.
    [[nodiscard]] std::vector<std::string> UndoLabels() const {
        std::vector<std::string> labels;
        for (const Step& step : m_undo) {
            labels.push_back(step.label);
        }
        return labels;
    }
    /// The state to go back to, and the current one kept for redo. Commits the open edit first.
    [[nodiscard]] std::optional<State> Undo(const State& current) { return Move(current, m_undo, m_redo); }
    [[nodiscard]] std::optional<State> Redo(const State& current) { return Move(current, m_redo, m_undo); }

    /// Forgets every step: a level was opened.
    void Clear() {
        m_open.reset();
        m_undo.clear();
        m_redo.clear();
        m_revision = 0;
        m_nextRevision = 1;
    }
    [[nodiscard]] Revision Current() const { return m_revision; }
    [[nodiscard]] std::size_t Steps() const { return m_undo.size(); }

    /// Steps beyond this are forgotten, oldest first.
    static constexpr std::size_t kMaxSteps = 200;

private:
    struct Step {
        std::string label;
        State state;
        Revision revision = 0;
    };

    static const std::string& Nothing() {
        static const std::string nothing;
        return nothing;
    }

    std::optional<State> Move(const State& current, std::vector<Step>& from, std::vector<Step>& to) {
        Commit(current);
        if (from.empty()) {
            return std::nullopt;
        }
        Step step = std::move(from.back());
        from.pop_back();
        to.push_back(Step{.label = step.label, .state = current, .revision = m_revision});
        m_revision = step.revision;
        return std::move(step.state);
    }

    std::optional<Step> m_open;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    Revision m_revision = 0;
    Revision m_nextRevision = 1;
};

/// A material edited in the material panel (ADR-0079): its model and its index there.
using MaterialKey = std::pair<const daidalos::scene::Model*, std::uint32_t>;

/// Everything the editor's undo covers (ADR-0109): the level, the values of the materials edited in the material
/// panel, and the sequence open in the sequence panel.
struct Document {
    daidalos::scene::Level level;
    /// The values of every material edited since the level opened. One edited later is not here in earlier
    /// states: the editor keeps what it was before its first edit.
    std::map<MaterialKey, daidalos::resources::MaterialData> materials;
    /// The sequence file open in its panel, and its cues; empty for none.
    std::string sequencePath;
    daidalos::scene::Sequence sequence;

    [[nodiscard]] bool operator==(const Document& other) const;
};

using History = UndoHistory<Document>;

/// Whether two materials' values are the same, as far as the material panel edits and files keep them.
[[nodiscard]] bool SameValues(const daidalos::resources::MaterialData& a, const daidalos::resources::MaterialData& b);

} // namespace editor
