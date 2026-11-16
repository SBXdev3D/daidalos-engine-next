// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Inspector.hpp"

#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

using namespace daidalos;

namespace editor {

namespace {

bool Degrees(const char* label, float& radians, float speed, float min, float max) {
    float degrees = glm::degrees(radians);
    if (ImGui::DragFloat(label, &degrees, speed, min, max, "%.2f deg")) {
        radians = glm::radians(degrees);
        return true;
    }
    return false;
}

/// A collapsing header with a button that removes the component. Returns whether it is open; removed is set
/// when the button was pressed.
bool Header(const char* label, bool& removed) {
    ImGui::PushID(label);
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
    if (ImGui::SmallButton("x")) {
        removed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove the component");
    }
    ImGui::PopID();
    return open && !removed;
}

bool InspectLight(scene::Light& light) {
    bool changed = false;
    int type = static_cast<int>(light.type);
    if (ImGui::Combo("Type", &type, "Point\0Spot\0Directional\0")) {
        light.type = static_cast<renderer::LightType>(type);
        changed = true;
    }
    const bool directional = light.type == renderer::LightType::Directional;
    changed |= ImGui::Checkbox("Enabled", &light.enabled);
    changed |= ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::DragFloat(directional ? "Illuminance (lux)" : "Intensity (cd)", &light.intensity, 1.0f, 0.0f,
                                1.0e6f, "%.2f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::DragFloat("Temperature (K)", &light.temperature, 10.0f, 0.0f, 40000.0f, "%.0f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The color of a black body, 2700 K for a warm bulb; 0 for none");
    }
    if (!directional) {
        changed |= ImGui::DragFloat("Range (m)", &light.range, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Radius (m)", &light.radius, 0.005f, 0.0f, 5.0f);
    }
    if (light.type == renderer::LightType::Spot) {
        changed |= Degrees("Inner angle", light.innerAngle, 0.2f, 0.0f, 89.0f);
        changed |= Degrees("Outer angle", light.outerAngle, 0.2f, 0.0f, 89.0f);
        light.innerAngle = std::min(light.innerAngle, light.outerAngle);
    }
    if (directional) {
        changed |= Degrees("Angular radius", light.angularRadius, 0.05f, 0.0f, 10.0f);
    }
    return changed;
}

bool InspectFog(scene::FogVolume& fog) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &fog.enabled);
    changed |= ImGui::DragFloat("Density (/m)", &fog.density, 0.005f, 0.0f, 50.0f);
    changed |= ImGui::ColorEdit3("Albedo", &fog.albedo.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::DragFloat("Edge", &fog.edge, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Falloff height (m)", &fog.falloffHeight, 0.01f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("Noise", &fog.noise, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Noise frequency", &fog.noiseFrequency, 0.01f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat3("Noise velocity (m/s)", &fog.noiseVelocity.x, 0.01f);
    changed |= ImGui::ColorEdit3("Glow (nits)", &fog.glow.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    return changed;
}

bool InspectWind(scene::WindVolume& wind) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &wind.enabled);
    changed |= ImGui::DragFloat3("Velocity (m/s)", &wind.velocity.x, 0.05f);
    changed |= ImGui::DragFloat("Gust", &wind.gust, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("Gust frequency (Hz)", &wind.gustFrequency, 0.01f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("Edge", &wind.edge, 0.01f, 0.0f, 1.0f);
    return changed;
}

bool InspectParticles(scene::LevelParticles& particles) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &particles.enabled);
    changed |= InputString("Effect file", particles.effect);
    bool burst = particles.burst.has_value();
    if (ImGui::Checkbox("Bursts only", &burst)) {
        particles.burst = burst
                              ? std::optional<scene::ParticleBurst>(scene::ParticleBurst{.count = 1, .interval = 1.0f})
                              : std::nullopt;
        changed = true;
    }
    if (particles.burst) {
        int count = static_cast<int>(particles.burst->count);
        if (ImGui::DragInt("Burst count", &count, 0.2f, 1, 100000)) {
            particles.burst->count = static_cast<std::uint32_t>(std::max(count, 1));
            changed = true;
        }
        changed |= ImGui::DragFloat("Burst interval (s)", &particles.burst->interval, 0.01f, 0.01f, 600.0f);
    }
    bool surface = particles.surface.has_value();
    if (ImGui::Checkbox("On a model's triangles", &surface)) {
        particles.surface =
            surface ? std::optional<scene::LevelNodeRef>(scene::LevelNodeRef{.entity = 1}) : std::nullopt;
        changed = true;
    }
    if (particles.surface) {
        int entity = static_cast<int>(particles.surface->entity);
        if (ImGui::InputInt("Entity id", &entity)) {
            particles.surface->entity = static_cast<std::uint64_t>(std::max(entity, 1));
            changed = true;
        }
        changed |= InputString("Node", particles.surface->node);
    }
    return changed;
}

bool InspectAttractor(scene::LevelAttractor& attractor) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &attractor.enabled);
    changed |= InputString("Only effect", attractor.effect);
    changed |= ImGui::DragFloat("Strength", &attractor.strength, 0.05f);
    changed |= ImGui::DragFloat("Vortex", &attractor.vortex, 0.05f);
    changed |= ImGui::DragFloat("Radius (m)", &attractor.radius, 0.05f, 0.0f, 100.0f);
    return changed;
}

bool InspectWeather(scene::LevelWeather& weather) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &weather.enabled);
    changed |= InputString("Effect file", weather.effect);
    changed |= ImGui::DragFloat("Rate (/m2/s)", &weather.rate, 0.5f, 0.0f, 10000.0f);
    changed |= ImGui::DragFloat("Reach (m)", &weather.reach, 0.1f, 0.0f, 200.0f);
    return changed;
}

bool EntityId(const char* label, std::uint64_t& id) {
    int value = static_cast<int>(id);
    if (ImGui::InputInt(label, &value)) {
        id = static_cast<std::uint64_t>(std::max(value, 0));
        return true;
    }
    return false;
}

bool InspectRibbon(scene::LevelRibbon& ribbon) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &ribbon.enabled);
    changed |= InputString("Ribbon file", ribbon.effect);
    changed |= EntityId("Target entity", ribbon.target);
    return changed;
}

bool InspectLightning(scene::LevelLightning& lightning) {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &lightning.enabled);
    changed |= InputString("Bolt ribbon", lightning.bolt);
    changed |= EntityId("Light entity", lightning.light);
    changed |= ImGui::DragFloat("Flash", &lightning.flash, 0.5f, 0.0f, 1000.0f);
    changed |= ImGui::DragFloat2("Interval (s)", &lightning.interval.x, 0.1f, 0.0f, 600.0f);
    changed |= ImGui::DragFloat("Duration (s)", &lightning.duration, 0.01f, 0.0f, 10.0f);
    return changed;
}

bool InspectGame(scene::Tags& tags) {
    bool changed = InputString("Kind", tags.kind);
    std::string removed;
    std::vector<std::pair<std::string, std::string>> renamed;
    for (auto& [key, value] : tags.values) {
        ImGui::PushID(key.c_str());
        std::string name = key;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        if (InputString("##key", name) && !name.empty() && name != key) {
            renamed.emplace_back(key, name);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        changed |= InputString("##value", value);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removed = key;
        }
        ImGui::PopID();
    }
    for (const auto& [from, to] : renamed) {
        if (!tags.values.contains(to)) {
            auto node = tags.values.extract(from);
            node.key() = to;
            tags.values.insert(std::move(node));
            changed = true;
        }
    }
    if (!removed.empty()) {
        tags.values.erase(removed);
        changed = true;
    }
    if (ImGui::SmallButton("Add a value")) {
        std::string key = "key";
        for (int number = 2; tags.values.contains(key); ++number) {
            key = "key" + std::to_string(number);
        }
        tags.values.emplace(key, "");
        changed = true;
    }
    return changed;
}

/// Runs one component's section: its header, and its fields while it is open.
template <class T, class Inspect>
bool Section(const char* label, std::optional<T>& component, Inspect&& inspect) {
    if (!component.has_value()) {
        return false;
    }
    bool removed = false;
    bool changed = false;
    if (Header(label, removed)) {
        ImGui::PushID(label);
        changed = inspect(*component);
        ImGui::PopID();
    }
    if (removed) {
        component.reset();
        return true;
    }
    return changed;
}

} // namespace

bool InputString(const char* label, std::string& value) {
    std::array<char, 512> buffer{};
    const std::size_t length = std::min(value.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), value.data(), length);
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool InspectComponents(scene::LevelEntity& entity) {
    bool changed = false;
    if (!entity.model.empty() || ImGui::GetIO().KeyAlt) {
        if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= InputString("glTF file", entity.model);
        }
    }
    changed |= Section("Light", entity.light, InspectLight);
    changed |= Section("Fog", entity.fog, InspectFog);
    changed |= Section("Wind", entity.wind, InspectWind);
    changed |= Section("Particles", entity.particles, InspectParticles);
    changed |= Section("Attractor", entity.attractor, InspectAttractor);
    changed |= Section("Weather", entity.weather, InspectWeather);
    changed |= Section("Ribbon", entity.ribbon, InspectRibbon);
    changed |= Section("Lightning", entity.lightning, InspectLightning);
    changed |= Section("Game", entity.game, InspectGame);

    ImGui::Separator();
    if (ImGui::Button("Add a component")) {
        ImGui::OpenPopup("add component");
    }
    if (ImGui::BeginPopup("add component")) {
        if (!entity.light && ImGui::MenuItem("Light")) {
            entity.light = scene::Light{.intensity = 100.0f};
            changed = true;
        }
        if (!entity.fog && ImGui::MenuItem("Fog")) {
            entity.fog = scene::FogVolume{};
            changed = true;
        }
        if (!entity.wind && ImGui::MenuItem("Wind")) {
            entity.wind = scene::WindVolume{.velocity = {1.0f, 0.0f, 0.0f}};
            changed = true;
        }
        if (!entity.particles && ImGui::MenuItem("Particles")) {
            entity.particles = scene::LevelParticles{.effect = "Effects/dust_motes.effect.json"};
            changed = true;
        }
        if (!entity.attractor && ImGui::MenuItem("Attractor")) {
            entity.attractor = scene::LevelAttractor{};
            changed = true;
        }
        if (!entity.weather && ImGui::MenuItem("Weather")) {
            entity.weather = scene::LevelWeather{.effect = "Effects/rain.effect.json"};
            changed = true;
        }
        if (!entity.ribbon && ImGui::MenuItem("Ribbon")) {
            entity.ribbon = scene::LevelRibbon{.effect = "Effects/arc.ribbon.json"};
            changed = true;
        }
        if (!entity.lightning && ImGui::MenuItem("Lightning")) {
            entity.lightning = scene::LevelLightning{.bolt = "Effects/lightning.ribbon.json"};
            changed = true;
        }
        if (!entity.game && ImGui::MenuItem("Game tags")) {
            entity.game = scene::Tags{.kind = "marker"};
            changed = true;
        }
        if (entity.model.empty() && ImGui::MenuItem("Model")) {
            entity.model = "Props/lamp.gltf";
            changed = true;
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool InspectEnvironment(scene::LevelEnvironment& environment) {
    bool changed = false;
    ImGui::SeparatorText("Sky");
    changed |= InputString("Faces", environment.skyFaces);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sky/sky1 reads Sky/sky1_right.png, _left, _up, _down, _front and _back");
    }
    changed |= ImGui::DragFloat("Luminance (nits)", &environment.skyIntensity, 0.1f, 0.0f, 100000.0f, "%.2f",
                                ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::DragFloat("Yaw (deg)", &environment.skyYaw, 0.5f, -360.0f, 360.0f);
    ImGui::SeparatorText("Fog");
    changed |= ImGui::DragFloat("Density (/m)", &environment.fogDensity, 0.0005f, 0.0f, 5.0f, "%.4f");
    changed |= ImGui::ColorEdit3("Albedo", &environment.fogAlbedo.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::DragFloat("Anisotropy", &environment.fogAnisotropy, 0.01f, -0.99f, 0.99f);
    ImGui::SeparatorText("Exposure");
    changed |= ImGui::DragFloat("EV100", &environment.ev100, 0.05f, -10.0f, 20.0f);
    changed |= ImGui::Checkbox("Eye adaptation", &environment.autoExposure);
    changed |= ImGui::DragFloat("Opens (stops)", &environment.exposureMin, 0.05f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("Closes (stops)", &environment.exposureMax, 0.05f, 0.0f, 10.0f);
    ImGui::SeparatorText("Wind");
    changed |= ImGui::DragFloat3("Velocity (m/s)", &environment.wind.x, 0.05f);
    changed |= ImGui::DragFloat("Gust", &environment.windGust, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("Gust frequency (Hz)", &environment.windGustFrequency, 0.01f, 0.0f, 10.0f);
    return changed;
}

const char* KindLabel(const scene::LevelEntity& entity) {
    if (entity.light) {
        return "L";
    }
    if (entity.particles) {
        return "P";
    }
    if (entity.fog) {
        return "F";
    }
    if (entity.weather) {
        return "W";
    }
    if (entity.wind) {
        return "~";
    }
    if (entity.ribbon || entity.lightning) {
        return "R";
    }
    if (entity.attractor) {
        return "A";
    }
    if (entity.game) {
        return "G";
    }
    if (!entity.model.empty()) {
        return "M";
    }
    return ".";
}

unsigned MarkerColor(const scene::LevelEntity& entity) {
    if (entity.light) {
        const glm::vec3 color = glm::clamp(entity.light->color, glm::vec3(0.25f), glm::vec3(1.0f));
        return IM_COL32(static_cast<int>(color.r * 255), static_cast<int>(color.g * 255),
                        static_cast<int>(color.b * 255), 255);
    }
    if (entity.particles || entity.ribbon || entity.lightning || entity.weather) {
        return IM_COL32(255, 160, 60, 255);
    }
    if (entity.fog || entity.wind) {
        return IM_COL32(140, 200, 255, 255);
    }
    if (entity.game) {
        return IM_COL32(120, 255, 120, 255);
    }
    return IM_COL32(200, 200, 200, 255);
}

} // namespace editor
