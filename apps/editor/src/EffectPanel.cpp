// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "EffectPanel.hpp"

#include <daidalos/scene/ParticleEffectFile.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

#include "Inspector.hpp"

using namespace daidalos;

namespace editor {

void EffectPanel::Open(const std::filesystem::path& assetRoot, std::string path) {
    std::ifstream file(assetRoot / std::filesystem::path(path), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    core::Result<scene::EffectDocument> document = scene::EffectDocument::Parse(text.str());
    if (!document) {
        m_status = core::Format(document.error());
        return;
    }
    m_path = std::move(path);
    m_ribbon = m_path.ends_with(".ribbon.json");
    m_document = std::move(*document);
    // As the panel would write it, so that opening a file writes nothing; Revert gives back the file as it was.
    m_text = m_document.Write();
    m_opened = text.str();
    m_edited = false;
    m_focus = true;
    m_error.clear();
    m_colorKey.reset();
    m_status = std::format("Editing {}: changes show at once", m_path);
}

void EffectPanel::Close() {
    m_path.clear();
}

void EffectPanel::SetText(const std::filesystem::path& assetRoot, const std::string& text) {
    core::Result<scene::EffectDocument> document = scene::EffectDocument::Parse(text);
    if (!document) {
        return;
    }
    m_document = std::move(*document);
    std::ofstream(assetRoot / std::filesystem::path(m_path), std::ios::binary) << text;
    m_text = text;
    m_error.clear();
}

void EffectPanel::Commit(const std::filesystem::path& assetRoot) {
    std::string text = m_document.Write();
    // Only what the game reads is written: a value it refuses stays here, with why.
    const core::Result<void> valid = m_ribbon ? scene::ParseRibbonEffect(text).map([](auto&&) {})
                                              : scene::ParseParticleEffect(text).map([](auto&&) {});
    if (!valid) {
        m_error = core::Format(valid.error());
        return;
    }
    m_error.clear();
    if (text != m_text) {
        std::ofstream(assetRoot / std::filesystem::path(m_path), std::ios::binary) << text;
        m_text = std::move(text);
        m_edited = true;
    }
}

bool EffectPanel::Number(const char* label, const char* key, float fallback, float speed, float low, float high,
                         const char* format) {
    float value = static_cast<float>(m_document.Number(key).value_or(fallback));
    if (ImGui::DragFloat(label, &value, speed, low, high, format)) {
        m_document.SetNumber(key, value);
        return true;
    }
    return false;
}

bool EffectPanel::Whole(const char* label, const char* key, int fallback, int low, int high) {
    int value = static_cast<int>(m_document.Number(key).value_or(fallback));
    if (ImGui::DragInt(label, &value, 0.1f, low, high)) {
        m_document.SetNumber(key, value);
        return true;
    }
    return false;
}

bool EffectPanel::Flag(const char* label, const char* key, bool fallback) {
    bool value = m_document.Flag(key).value_or(fallback);
    if (ImGui::Checkbox(label, &value)) {
        m_document.SetFlag(key, value);
        return true;
    }
    return false;
}

bool EffectPanel::Range(const char* label, const char* key, float fallback, float speed, float low, float high) {
    const std::pair<double, double> range = m_document.Range(key).value_or(std::pair{fallback, fallback});
    float values[2] = {static_cast<float>(range.first), static_cast<float>(range.second)};
    if (ImGui::DragFloat2(label, values, speed, low, high, "%.3f")) {
        m_document.SetRange(key, values[0], std::max(values[0], values[1]));
        return true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Each particle picks between the two");
    }
    return false;
}

bool EffectPanel::Vector(const char* label, const char* key, const glm::vec3& fallback, float speed) {
    glm::vec3 value = m_document.Vector(key).value_or(fallback);
    if (ImGui::DragFloat3(label, &value.x, speed)) {
        m_document.SetVector(key, value);
        return true;
    }
    return false;
}

bool EffectPanel::Choice(const char* label, const char* key, const std::vector<const char*>& choices) {
    const std::string current = m_document.Text(key).value_or(choices.front());
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (const char* choice : choices) {
            if (ImGui::Selectable(choice, current == choice)) {
                m_document.SetText(key, choice);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool EffectPanel::Text(const char* label, const char* key) {
    std::string value = m_document.Text(key).value_or("");
    if (InputString(label, value)) {
        if (value.empty()) {
            m_document.Remove(key);
        } else {
            m_document.SetText(key, value);
        }
        return true;
    }
    return false;
}

bool EffectPanel::CurveField(const char* label, const char* key, float fallback) {
    // A curve over life: drag a key, double-click to add one, right-click a key to take it away.
    std::vector<std::pair<float, float>> keys =
        m_document.Curve(key).value_or(std::vector<std::pair<float, float>>{{0.0f, fallback}});
    ImGui::TextUnformatted(label);
    const float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
    const ImVec2 size(width, 70.0f);
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(key, size);
    const bool hovered = ImGui::IsItemHovered();
    float top = 1.0f;
    float bottom = 0.0f;
    for (const auto& [time, value] : keys) {
        top = std::max(top, value);
        bottom = std::min(bottom, value);
    }
    top += (top - bottom) * 0.15f;
    const auto toScreen = [&](float time, float value) {
        return ImVec2(corner.x + time * size.x, corner.y + (top - value) / (top - bottom) * size.y);
    };
    const auto fromScreen = [&](const ImVec2& at) {
        return std::pair{std::clamp((at.x - corner.x) / size.x, 0.0f, 1.0f),
                         top - (at.y - corner.y) / size.y * (top - bottom)};
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(corner, {corner.x + size.x, corner.y + size.y}, IM_COL32(25, 25, 30, 255), 3.0f);
    const ImVec2 zero = toScreen(0.0f, 0.0f);
    draw->AddLine({corner.x, zero.y}, {corner.x + size.x, zero.y}, IM_COL32(70, 70, 80, 255));
    for (int step = 0; step < 64; ++step) {
        const float a = static_cast<float>(step) / 64.0f;
        const float b = static_cast<float>(step + 1) / 64.0f;
        draw->AddLine(toScreen(a, CurveAt(keys, a)), toScreen(b, CurveAt(keys, b)), IM_COL32(255, 200, 90, 255), 2.0f);
    }
    bool changed = false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    std::optional<std::size_t> under;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const ImVec2 at = toScreen(keys[index].first, keys[index].second);
        const bool near = std::hypot(at.x - mouse.x, at.y - mouse.y) < 7.0f;
        under = near && hovered ? std::optional(index) : under;
        draw->AddCircleFilled(at, near ? 5.5f : 4.0f, IM_COL32(255, 230, 160, 255));
    }
    if (hovered && under && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_dragging = std::pair{std::string(key), *under};
    }
    if (m_dragging && m_dragging->first == key) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_dragging->second < keys.size()) {
            const auto [time, value] = fromScreen(mouse);
            keys[m_dragging->second] = {time, std::max(value, bottom)};
            changed = true;
        } else {
            m_dragging.reset();
        }
    }
    if (hovered && !under && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        keys.push_back(fromScreen(mouse));
        changed = true;
    }
    if (hovered && under && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && keys.size() > 1) {
        keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*under));
        changed = true;
    }
    if (hovered && under) {
        ImGui::SetTooltip("%.2f of its life: %.3f", keys[*under].first, keys[*under].second);
    }
    if (changed) {
        // A dragged key keeps its place in the list while dragged; it is sorted when let go.
        if (!m_dragging) {
            keys = SortedKeys(std::move(keys));
        }
        m_document.SetCurve(key, keys);
    }
    return changed;
}

bool EffectPanel::ColorsField(const char* label, const char* key) {
    // A gradient over life: click a key under it to change its color, double-click the bar to add one.
    std::vector<std::pair<float, glm::vec4>> keys = m_document.Colors(key).value_or(
        std::vector<std::pair<float, glm::vec4>>{{0.0f, glm::vec4(1.0f)}, {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}});
    ImGui::TextUnformatted(label);
    const float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f);
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(key, ImVec2(width, 28.0f));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto colorAt = [&keys](float time) {
        if (time <= keys.front().first) {
            return keys.front().second;
        }
        for (std::size_t index = 1; index < keys.size(); ++index) {
            if (time <= keys[index].first) {
                const float span = keys[index].first - keys[index - 1].first;
                return glm::mix(keys[index - 1].second, keys[index].second,
                                span > 0.0f ? (time - keys[index - 1].first) / span : 1.0f);
            }
        }
        return keys.back().second;
    };
    const auto packed = [](const glm::vec4& color) {
        const glm::vec4 shown = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f)) * 255.0f;
        return IM_COL32(static_cast<int>(shown.r), static_cast<int>(shown.g), static_cast<int>(shown.b),
                        static_cast<int>(shown.a));
    };
    draw->AddRectFilled(corner, {corner.x + width, corner.y + 18.0f}, IM_COL32(60, 60, 60, 255));
    for (int step = 0; step < 48; ++step) {
        const float a = static_cast<float>(step) / 48.0f;
        const float b = static_cast<float>(step + 1) / 48.0f;
        draw->AddRectFilledMultiColor({corner.x + a * width, corner.y}, {corner.x + b * width, corner.y + 18.0f},
                                      packed(colorAt(a)), packed(colorAt(b)), packed(colorAt(b)), packed(colorAt(a)));
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool changed = false;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const float x = corner.x + keys[index].first * width;
        const bool chosen = m_colorKey == index;
        draw->AddTriangleFilled({x, corner.y + 18.0f}, {x - 5.0f, corner.y + 27.0f}, {x + 5.0f, corner.y + 27.0f},
                                chosen ? IM_COL32(255, 200, 60, 255) : IM_COL32(220, 220, 220, 255));
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && std::abs(mouse.x - x) < 6.0f &&
            mouse.y > corner.y + 16.0f) {
            m_colorKey = index;
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mouse.y < corner.y + 18.0f) {
        const float time = std::clamp((mouse.x - corner.x) / width, 0.0f, 1.0f);
        keys.emplace_back(time, colorAt(time));
        std::ranges::stable_sort(keys, {}, &std::pair<float, glm::vec4>::first);
        changed = true;
    }
    if (m_colorKey && *m_colorKey < keys.size()) {
        auto& [time, color] = keys[*m_colorKey];
        ImGui::PushID(key);
        changed |= ImGui::ColorEdit4("Color", &color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        changed |= ImGui::SliderFloat("At", &time, 0.0f, 1.0f, "%.2f of its life");
        if (keys.size() > 1 && ImGui::SmallButton("Take this key away")) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*m_colorKey));
            m_colorKey.reset();
            changed = true;
        }
        ImGui::PopID();
    }
    if (changed) {
        m_document.SetColors(key, keys);
    }
    return changed;
}

void EffectPanel::DrawParticles() {
    bool changed = false;
    if (ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= Text("Texture", "texture");
        changed |= Whole("Columns", "columns", 1, 1, 64);
        changed |= Whole("Rows", "rows", 0, 0, 64);
        changed |= Whole("First cell", "cell", 0, 0, 4095);
        changed |= Whole("Frames", "frames", 1, 1, 4096);
        changed |= Number("Frame rate", "frameRate", 0.0f, 0.1f, 0.0f, 240.0f);
        changed |= Flag("Loop", "loop", true);
        changed |= Flag("Start at a random frame", "randomStartFrame", false);
        changed |= Flag("Adds light", "additive", false);
        changed |= Flag("Lit by the lamps", "lit", true);
        changed |= Flag("Seen by rays", "traced", true);
        changed |= Number("Softness (m)", "softness", 0.3f, 0.005f, 0.0f, 5.0f);
        changed |= Number("Fog it adds", "fogDensity", 0.0f, 0.01f, 0.0f, 10.0f);
        changed |= Number("Light's direction", "anisotropy", 0.0f, 0.01f, -1.0f, 1.0f);
        changed |= Number("Distortion", "distortion", 0.0f, 0.001f, 0.0f, 0.2f);
        changed |= Text("Mesh", "mesh");
    }
    if (ImGui::CollapsingHeader("Life", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= Range("Lifetime (s)", "lifetime", 1.0f, 0.01f, 0.0f, 600.0f);
        changed |= Range("Size (m)", "size", 0.1f, 0.001f, 0.0f, 100.0f);
        changed |= Number("Fade in", "fadeIn", 0.1f, 0.005f, 0.0f, 1.0f);
        changed |= ColorsField("Color over life", "color");
        changed |= CurveField("Size over life", "sizeOverLife", 1.0f);
        changed |= CurveField("Glow over life", "emissionOverLife", 1.0f);
        changed |= CurveField("Speed over life", "speedOverLife", 1.0f);
    }
    if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= Range("Speed (m/s)", "speed", 0.0f, 0.01f, 0.0f, 500.0f);
        changed |= Vector("Direction", "direction", glm::vec3(0.0f, 1.0f, 0.0f), 0.01f);
        changed |= Number("Spread (deg)", "spread", 0.0f, 0.5f, 0.0f, 180.0f, "%.1f");
        changed |= Number("Gravity", "gravity", 0.0f, 0.01f, -10.0f, 10.0f);
        changed |= Number("Drag", "drag", 0.0f, 0.01f, 0.0f, 50.0f);
        changed |= Number("Turbulence", "turbulence", 0.0f, 0.01f, 0.0f, 100.0f);
        changed |= Number("Turbulence size (m)", "turbulenceScale", 1.0f, 0.01f, 0.01f, 100.0f);
        changed |= Number("Takes the emitter's velocity", "inheritVelocity", 0.0f, 0.01f, 0.0f, 1.0f);
        changed |= Number("Turning (deg/s)", "rotationSpeed", 0.0f, 1.0f, 0.0f, 3600.0f, "%.0f");
        changed |= Choice("Facing", "orientation", {"camera", "velocity", "axis", "plane"});
        changed |= Number("Stretch (s)", "stretch", 0.0f, 0.001f, 0.0f, 1.0f);
        changed |= Vector("Axis", "axis", glm::vec3(0.0f, 1.0f, 0.0f), 0.01f);
    }
    if (ImGui::CollapsingHeader("Where they start")) {
        changed |= Choice("Shape", "shape", {"box", "cylinder", "cone", "line"});
        changed |= Vector("Box (half, m)", "spawnBox", glm::vec3(0.0f), 0.01f);
        changed |= Number("Radius (m)", "spawnRadius", 0.0f, 0.01f, 0.0f, 100.0f);
    }
    if (ImGui::CollapsingHeader("Surfaces")) {
        changed |= Choice("When they meet one", "collision", {"none", "bounce", "stick", "die"});
        changed |= Number("Bounce", "bounce", 0.3f, 0.01f, 0.0f, 1.0f);
        changed |= Flag("Report where they hit", "reportHits", false);
    }
    if (ImGui::CollapsingHeader("Emitter")) {
        changed |= Number("Per second", "emitter.rate", 10.0f, 0.1f, 0.0f, 100000.0f, "%.1f");
        changed |= Number("Per meter moved", "emitter.ratePerMeter", 0.0f, 0.1f, 0.0f, 10000.0f, "%.1f");
        changed |= Number("Seen within (m)", "emitter.distance", 50.0f, 0.5f, 0.0f, 1000.0f, "%.0f");
        changed |= Number("Its own size (m)", "emitter.radius", 2.0f, 0.05f, 0.0f, 100.0f);
    }
    if (ImGui::CollapsingHeader("Its light")) {
        bool lit = m_document.Has("light");
        if (ImGui::Checkbox("Gives light", &lit)) {
            if (lit) {
                m_document.SetNumber("light.intensity", 10.0);
            } else {
                m_document.Remove("light");
            }
            changed = true;
        }
        if (lit) {
            glm::vec3 color = m_document.Vector("light.color").value_or(glm::vec3(1.0f));
            if (ImGui::ColorEdit3("Light color", &color.x)) {
                m_document.SetVector("light.color", color);
                changed = true;
            }
            changed |= Number("Intensity (cd)", "light.intensity", 10.0f, 0.5f, 0.0f, 100000.0f, "%.1f");
            changed |= Number("Range (m)", "light.range", 5.0f, 0.05f, 0.0f, 100.0f);
            changed |= Number("Flicker rate", "light.flickerRate", 0.0f, 0.1f, 0.0f, 60.0f);
            changed |= Number("Flicker depth", "light.flickerDepth", 0.0f, 0.01f, 0.0f, 1.0f);
        }
    }
    if (changed) {
        m_status.clear();
    }
}

void EffectPanel::DrawRibbon() {
    if (ImGui::CollapsingHeader("Ribbon", ImGuiTreeNodeFlags_DefaultOpen)) {
        Text("Texture", "texture");
        Choice("Shape", "shape", {"line", "bolt", "trail"});
        Flag("Adds light", "additive", true);
        Number("Softness (m)", "softness", 0.05f, 0.005f, 0.0f, 2.0f);
        ColorsField("Color along it", "color");
        CurveField("Width along it (m)", "width", 0.05f);
        Number("Texture repeats per meter", "tiling", 1.0f, 0.05f, 0.0f, 100.0f);
        Number("Scroll (per second)", "scroll", 0.0f, 0.05f, -100.0f, 100.0f);
    }
    if (ImGui::CollapsingHeader("Bolt and trail")) {
        Whole("Generations", "generations", 5, 1, 10);
        Number("Displacement", "displacement", 0.15f, 0.005f, 0.0f, 1.0f);
        Number("Branches", "branches", 0.0f, 0.01f, 0.0f, 1.0f);
        Number("Refresh (s)", "refresh", 0.05f, 0.005f, 0.0f, 5.0f);
        Number("Flicker", "flicker", 0.0f, 0.01f, 0.0f, 1.0f);
        Number("Trail duration (s)", "duration", 0.5f, 0.01f, 0.0f, 10.0f);
        Number("Trail spacing (m)", "spacing", 0.05f, 0.005f, 0.001f, 5.0f);
    }
}

void EffectPanel::Draw(const std::filesystem::path& assetRoot) {
    if (std::exchange(m_focus, false)) {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Effect")) {
        ImGui::End();
        return;
    }
    if (!IsOpen()) {
        ImGui::TextWrapped("No effect open. Right-click an effect or a ribbon in the Assets panel, or use \"Edit its "
                           "effect\" in the inspector of an emitter.");
        if (!m_status.empty()) {
            ImGui::TextWrapped("%s", m_status.c_str());
        }
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", m_path.c_str());
    if (const std::optional<std::string> comment = m_document.Text("comment")) {
        ImGui::TextWrapped("%s", comment->c_str());
    }
    ImGui::BeginDisabled(!m_edited);
    if (ImGui::Button("Revert")) {
        SetText(assetRoot, m_opened);
        m_text = m_document.Write();
        m_edited = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        Close();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(m_edited ? "written as changed" : "as opened");
    if (!m_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Not written: %s", m_error.c_str());
    }
    ImGui::Separator();
    if (m_ribbon) {
        DrawRibbon();
    } else {
        DrawParticles();
    }
    Commit(assetRoot);
    ImGui::End();
}

} // namespace editor
