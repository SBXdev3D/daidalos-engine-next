// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "SequencePanel.hpp"

#include <daidalos/logging/Log.hpp>

#include <glm/geometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

#include "Cues.hpp"
#include "Inspector.hpp"

using namespace daidalos;

namespace editor {

namespace {

constexpr std::string_view kModule = "editor";
constexpr float kTimelineHeight = 64.0f; // Interface pixels.
constexpr float kMarkerHalfWidth = 4.0f;

} // namespace

void SequencePanel::Open(const std::filesystem::path& assetRoot, std::string path) {
    m_path = std::move(path);
    m_sequence = {};
    m_selected.reset();
    m_playhead.reset();
    m_cursor = 0.0f;
    std::ifstream stream(assetRoot / std::filesystem::path(m_path), std::ios::binary);
    if (!stream) {
        m_saved = {};
        m_status = std::format("{} is new: save it to make the file", m_path);
        return;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    core::Result<scene::Sequence> read = scene::ParseSequence(text.str(), m_path);
    if (!read) {
        m_status = core::Format(read.error());
        DAI_LOG_WARNING(kModule, "{}", m_status);
        m_saved = {};
        return;
    }
    m_sequence = std::move(*read);
    m_saved = m_sequence;
    m_status = std::format("Opened {}: {} cues", m_path, m_sequence.cues.size());
}

void SequencePanel::Close() {
    m_path.clear();
    m_sequence = {};
    m_saved = {};
    m_selected.reset();
}

void SequencePanel::SetSequence(scene::Sequence sequence) {
    m_sequence = std::move(sequence);
    if (m_selected && *m_selected >= m_sequence.cues.size()) {
        m_selected.reset();
    }
}

bool SequencePanel::Save(const std::filesystem::path& assetRoot) {
    if (m_path.empty()) {
        return true;
    }
    const std::filesystem::path file = assetRoot / std::filesystem::path(m_path);
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const std::string text = scene::WriteSequence(m_sequence);
    std::filesystem::path temporary = file;
    temporary += ".saving";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            m_status = std::format("Cannot write {}", temporary.generic_string());
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        m_status = std::format("Cannot replace {}: {}", file.generic_string(), error.message());
        return false;
    }
    m_saved = m_sequence;
    m_status = std::format("Saved {}", m_path);
    DAI_LOG_INFO(kModule, "{}", m_status);
    return true;
}

const scene::Cue* SequencePanel::Selected() const {
    return m_selected && *m_selected < m_sequence.cues.size() ? &m_sequence.cues[*m_selected] : nullptr;
}

void SequencePanel::Sort() {
    if (!m_selected || *m_selected >= m_sequence.cues.size()) {
        std::ranges::stable_sort(m_sequence.cues, {}, &scene::Cue::at);
        return;
    }
    // The selected cue is found again by its place among cues of the same time and values.
    const scene::Cue selected = m_sequence.cues[*m_selected];
    std::ranges::stable_sort(m_sequence.cues, {}, &scene::Cue::at);
    const auto found = std::ranges::find(m_sequence.cues, selected);
    m_selected = static_cast<std::size_t>(found - m_sequence.cues.begin());
}

void SequencePanel::DrawTimeline() {
    const float length = std::max(m_sequence.Length(), 1.0f) + 1.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
    const float scale = width / length; // Pixels per second.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + width, origin.y + kTimelineHeight}, IM_COL32(25, 25, 30, 255));
    // A tick every second, labelled every few.
    const int labelEvery = std::max(1, static_cast<int>(std::ceil(40.0f / scale)));
    for (int second = 0; static_cast<float>(second) <= length; ++second) {
        const float x = origin.x + static_cast<float>(second) * scale;
        draw->AddLine({x, origin.y + kTimelineHeight - 8.0f}, {x, origin.y + kTimelineHeight},
                      IM_COL32(110, 110, 120, 255));
        if (second % labelEvery == 0) {
            draw->AddText({x + 2.0f, origin.y + kTimelineHeight - 20.0f}, IM_COL32(140, 140, 150, 255),
                          std::format("{}s", second).c_str());
        }
    }
    // The cursor, where new cues go, and the playhead of a preview.
    const float cursorX = origin.x + m_cursor * scale;
    draw->AddLine({cursorX, origin.y}, {cursorX, origin.y + kTimelineHeight}, IM_COL32(255, 255, 255, 120));
    if (m_playhead) {
        const float x = origin.x + *m_playhead * scale;
        draw->AddLine({x, origin.y}, {x, origin.y + kTimelineHeight}, IM_COL32(255, 80, 80, 255), 2.0f);
    }

    // Cues: a marker each, stacked when they share a time, dragged to move in time.
    bool overMarker = false;
    for (std::size_t index = 0; index < m_sequence.cues.size(); ++index) {
        scene::Cue& cue = m_sequence.cues[index];
        std::size_t row = 0;
        for (std::size_t before = 0; before < index; ++before) {
            row += std::abs(m_sequence.cues[before].at - cue.at) * scale < kMarkerHalfWidth * 2.0f ? 1 : 0;
        }
        const float x = origin.x + cue.at * scale;
        const float y = origin.y + 4.0f + static_cast<float>(row % 4) * 10.0f;
        ImGui::SetCursorScreenPos({x - kMarkerHalfWidth, y});
        ImGui::PushID(static_cast<int>(index));
        ImGui::InvisibleButton("cue", {kMarkerHalfWidth * 2.0f, 9.0f});
        const bool selected = m_selected == index;
        if (ImGui::IsItemActivated()) {
            m_selected = index;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            cue.at = std::max(0.0f, (ImGui::GetIO().MousePos.x - origin.x) / scale);
            if (ImGui::GetIO().KeyCtrl) {
                cue.at = std::round(cue.at * 10.0f) / 10.0f;
            }
        }
        if (ImGui::IsItemDeactivated()) {
            Sort();
        }
        if (ImGui::IsItemHovered()) {
            overMarker = true;
            ImGui::SetTooltip("%.2f s: %s\nDrag to move it in time (Ctrl: by tenths)", cue.at, CueSummary(cue).c_str());
        }
        ImGui::PopID();
        const unsigned color = CueColor(cue.kind);
        draw->AddRectFilled({x - kMarkerHalfWidth, y}, {x + kMarkerHalfWidth, y + 9.0f}, color, 2.0f);
        if (selected) {
            draw->AddRect({x - kMarkerHalfWidth - 2.0f, y - 2.0f}, {x + kMarkerHalfWidth + 2.0f, y + 11.0f},
                          IM_COL32(255, 255, 255, 255), 2.0f, 0, 1.5f);
        }
        // What lasts shows as a bar.
        const float lasts =
            cue.kind == scene::Cue::Kind::Camera && !cue.keys.empty() ? cue.keys.back().at : cue.seconds;
        if (lasts > 0.0f) {
            draw->AddLine({x, y + 4.5f}, {x + lasts * scale, y + 4.5f}, (color & 0x00FFFFFFU) | 0x80000000U, 2.0f);
        }
    }
    // A click elsewhere puts the cursor there.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("timeline", {width, kTimelineHeight});
    if (ImGui::IsItemClicked() && !overMarker) {
        m_cursor = std::max(0.0f, (ImGui::GetIO().MousePos.x - origin.x) / scale);
    }
}

bool SequencePanel::EntityField(const char* label, std::string& name, const Context& context) {
    bool changed = InputString(label, name);
    if (context.level == nullptr) {
        return changed;
    }
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##pick", nullptr, ImGuiComboFlags_NoPreview)) {
        for (const scene::LevelEntity& entity : context.level->entities) {
            if (!entity.name.empty() && ImGui::Selectable(entity.name.c_str(), entity.name == name)) {
                name = entity.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

bool SequencePanel::DrawCue(scene::Cue& cue, const Context& context) {
    bool changed = ImGui::DragFloat("At (s)", &cue.at, 0.01f, 0.0f, 600.0f, "%.2f");
    const auto toggle = [&](const char* label) { changed |= ImGui::Checkbox(label, &cue.on); };
    switch (cue.kind) {
    case scene::Cue::Kind::Power:
        changed |= InputString("Circuit", cue.name);
        toggle("On");
        break;
    case scene::Cue::Kind::Light:
        changed |= EntityField("Light", cue.name, context);
        toggle("On");
        break;
    case scene::Cue::Kind::Visible:
        changed |= EntityField("Entity", cue.name, context);
        toggle("Visible");
        break;
    case scene::Cue::Kind::Sound:
        changed |= InputString("Clip", cue.path);
        changed |= EntityField("At entity", cue.name, context);
        changed |= ImGui::SliderFloat("Volume", &cue.volume, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Ducking", &cue.ducking, 0.0f, 1.0f);
        changed |= InputString("Caption", cue.caption);
        break;
    case scene::Cue::Kind::Subtitle:
        changed |= InputString("Speaker", cue.name);
        changed |= InputString("Text", cue.caption);
        changed |= ImGui::DragFloat("Seconds", &cue.seconds, 0.05f, 0.1f, 30.0f, "%.2f");
        ImGui::TextDisabled("Keys of the text table (ADR-0075): speaker.*, subtitle.*");
        break;
    case scene::Cue::Kind::Particles: {
        changed |= InputString("Effect", cue.path);
        changed |= EntityField("At entity", cue.name, context);
        int count = static_cast<int>(cue.count);
        if (ImGui::DragInt("Count", &count, 1.0f, 1, 10000)) {
            cue.count = static_cast<std::uint32_t>(std::max(count, 1));
            changed = true;
        }
        break;
    }
    case scene::Cue::Kind::Move:
        changed |= EntityField("Entity", cue.name, context);
        changed |= ImGui::DragFloat3("To (m)", &cue.position.x, 0.01f);
        changed |= ImGui::DragFloat("Seconds", &cue.seconds, 0.05f, 0.0f, 60.0f, "%.2f");
        if (ImGui::Button("To where the camera looks")) {
            cue.position = context.cameraPosition + context.cameraForward * 3.0f;
            changed = true;
        }
        break;
    case scene::Cue::Kind::Shake:
        changed |= ImGui::SliderFloat("Strength", &cue.strength, 0.0f, 2.0f);
        changed |= ImGui::DragFloat("Seconds", &cue.seconds, 0.05f, 0.0f, 10.0f, "%.2f");
        break;
    case scene::Cue::Kind::Mix:
        changed |= ImGui::SliderFloat("Effects", &cue.mix.buses[0], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Music", &cue.mix.buses[1], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Ambience", &cue.mix.buses[2], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Interface", &cue.mix.buses[3], 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Low-pass (Hz)", &cue.mix.lowpass, 10.0f, 20.0f, 20000.0f, "%.0f",
                                    ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::DragFloat("Fade (s)", &cue.seconds, 0.05f, 0.0f, 30.0f, "%.2f");
        break;
    case scene::Cue::Kind::Fade:
        changed |= ImGui::SliderFloat("To", &cue.strength, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How much the color covers the picture: 1 all of it, 0 none");
        }
        changed |= ImGui::DragFloat("Seconds", &cue.seconds, 0.05f, 0.0f, 30.0f, "%.2f");
        changed |= ImGui::ColorEdit3("Color", &cue.color.x);
        break;
    case scene::Cue::Kind::Event:
        changed |= InputString("Name", cue.name);
        ImGui::TextDisabled("The game's code takes it: checkpoint, stinger...");
        break;
    case scene::Cue::Kind::Camera: {
        bool smooth = cue.cameraPath == scene::CameraPath::Smooth;
        if (ImGui::Checkbox("Smooth path", &smooth)) {
            cue.cameraPath = smooth ? scene::CameraPath::Smooth : scene::CameraPath::Eased;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("One curve through every key without stopping, for long flights; off, the camera "
                              "eases into each key, a shot at a time");
        }
        std::optional<std::size_t> remove;
        for (std::size_t index = 0; index < cue.keys.size(); ++index) {
            scene::CameraKey& key = cue.keys[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SeparatorText(std::format("Key {}", index + 1).c_str());
            changed |= ImGui::DragFloat("At (s)", &key.at, 0.01f, 0.0f, 600.0f, "%.2f");
            changed |= ImGui::DragFloat3("Position", &key.position.x, 0.01f);
            changed |= ImGui::DragFloat3("Looks at", &key.target.x, 0.01f);
            changed |= ImGui::DragFloat("Aperture (f/)", &key.aperture, 0.05f, 0.0f, 22.0f, "%.1f");
            changed |= ImGui::DragFloat("Focus (m)", &key.focus, 0.05f, 0.1f, 100.0f, "%.2f");
            if (ImGui::Button("From the view")) {
                key.position = context.cameraPosition;
                key.target = context.cameraPosition + context.cameraForward * std::max(key.focus, 1.0f);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(cue.keys.size() <= 1);
            if (ImGui::Button("Remove")) {
                remove = index;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (remove) {
            cue.keys.erase(cue.keys.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
        if (ImGui::Button("Add a key from the view")) {
            scene::CameraKey key{.at = cue.keys.empty() ? 0.0f : cue.keys.back().at + 1.0f,
                                 .position = context.cameraPosition,
                                 .target = context.cameraPosition + context.cameraForward * 3.0f};
            cue.keys.push_back(key);
            changed = true;
        }
        std::ranges::sort(cue.keys, {}, &scene::CameraKey::at);
        break;
    }
    }
    return changed;
}

SequencePanel::Request SequencePanel::Draw(const Context& context) {
    Request request = Request::None;
    if (m_path.empty()) {
        return request;
    }
    bool open = true;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 520.0f), ImGuiCond_FirstUseEver);
    const std::string title = std::format("Sequence{}###Sequence", Dirty() ? " *" : "");
    if (!ImGui::Begin(title.c_str(), &open)) {
        ImGui::End();
        return request;
    }
    ImGui::TextDisabled("%s", m_path.c_str());
    if (ImGui::Button("Play")) {
        request = Request::Play;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Plays it in the view as it is, saved or not. The editor makes no sound.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Put the scene back")) {
        request = Request::Reset;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Makes the level's entities again: lights, moves and hides of a preview are undone");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!Dirty());
    if (ImGui::Button("Save")) {
        Save(context.assetRoot);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        Open(context.assetRoot, std::string(m_path));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s, %zu cues", m_sequence.Length(), m_sequence.cues.size());

    InputString("Comment", m_sequence.comment);
    DrawTimeline();

    // Adding at the cursor, and what to do with the selected cue.
    ImGui::SetNextItemWidth(140.0f);
    const std::string kindName(scene::CueKindName(kCueKinds[static_cast<std::size_t>(m_newKind)]));
    if (ImGui::BeginCombo("##kind", kindName.c_str())) {
        for (std::size_t index = 0; index < kCueKinds.size(); ++index) {
            const std::string name(scene::CueKindName(kCueKinds[index]));
            if (ImGui::Selectable(name.c_str(), static_cast<int>(index) == m_newKind)) {
                m_newKind = static_cast<int>(index);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(std::format("Add at {:.2f} s", m_cursor).c_str())) {
        m_sequence.cues.push_back(MakeCue(kCueKinds[static_cast<std::size_t>(m_newKind)], m_cursor));
        m_selected = m_sequence.cues.size() - 1;
        Sort();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(Selected() == nullptr);
    if (ImGui::Button("Duplicate")) {
        scene::Cue copy = *Selected();
        copy.at += 0.5f;
        m_sequence.cues.push_back(copy);
        m_selected = m_sequence.cues.size() - 1;
        Sort();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        m_sequence.cues.erase(m_sequence.cues.begin() + static_cast<std::ptrdiff_t>(*m_selected));
        m_selected.reset();
    }
    ImGui::EndDisabled();

    // The list, and the selected cue's fields beside it.
    if (ImGui::BeginTable("cues", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        ImGui::BeginChild("list", ImVec2(0.0f, 0.0f));
        for (std::size_t index = 0; index < m_sequence.cues.size(); ++index) {
            const scene::Cue& cue = m_sequence.cues[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(CueColor(cue.kind)));
            if (ImGui::Selectable(std::format("{:6.2f}  {}", cue.at, CueSummary(cue)).c_str(), m_selected == index)) {
                m_selected = index;
                m_cursor = cue.at;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("cue", ImVec2(0.0f, 0.0f));
        if (m_selected && *m_selected < m_sequence.cues.size()) {
            scene::Cue& cue = m_sequence.cues[*m_selected];
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(CueColor(cue.kind)), "%s",
                               std::string(scene::CueKindName(cue.kind)).c_str());
            if (DrawCue(cue, context) && !ImGui::IsAnyItemActive()) {
                Sort();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                Sort();
            }
        } else {
            ImGui::TextDisabled("Select a cue, or add one at the cursor.");
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if (!m_status.empty()) {
        ImGui::TextWrapped("%s", m_status.c_str());
    }
    ImGui::End();
    if (!open) {
        if (Dirty()) {
            m_status = "Save or revert the sequence before closing it";
        } else {
            Close();
        }
    }
    return request;
}

} // namespace editor
