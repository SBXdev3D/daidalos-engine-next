// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "History.hpp"

#include <utility>

using namespace daidalos;

namespace editor {

namespace {

const std::string kNothing;

} // namespace

void History::Begin(const scene::Level& current, std::string_view label) {
    if (!m_open) {
        m_open = Step{.label = std::string(label), .level = current, .revision = m_revision};
    }
}

void History::Commit(const scene::Level& current) {
    if (!m_open) {
        return;
    }
    Step step = std::move(*m_open);
    m_open.reset();
    if (step.level == current) {
        return;
    }
    m_undo.push_back(std::move(step));
    if (m_undo.size() > kMaxSteps) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_revision = m_nextRevision++;
}

const std::string& History::UndoLabel() const {
    return m_undo.empty() ? kNothing : m_undo.back().label;
}

const std::string& History::RedoLabel() const {
    return m_redo.empty() ? kNothing : m_redo.back().label;
}

std::optional<scene::Level> History::Undo(const scene::Level& current) {
    Commit(current);
    if (m_undo.empty()) {
        return std::nullopt;
    }
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(Step{.label = step.label, .level = current, .revision = m_revision});
    m_revision = step.revision;
    return std::move(step.level);
}

std::optional<scene::Level> History::Redo(const scene::Level& current) {
    Commit(current);
    if (m_redo.empty()) {
        return std::nullopt;
    }
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(Step{.label = step.label, .level = current, .revision = m_revision});
    m_revision = step.revision;
    return std::move(step.level);
}

void History::Clear() {
    m_open.reset();
    m_undo.clear();
    m_redo.clear();
    m_revision = 0;
    m_nextRevision = 1;
}

} // namespace editor
