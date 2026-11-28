// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Inspector.hpp"

#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Bodies.hpp>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
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
    if (!directional) {
        changed |= InputString("Profile (IES)", light.profile);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "How a real lamp sends its light around its axis, -Z: an IES file, relative to the level");
        }
    }
    if (light.type == renderer::LightType::Spot) {
        changed |= InputString("Cookie", light.cookie);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A grey image the light shines through, relative to the level; its up is +Y");
        }
    }
    // What changes its brightness, and its lamp's glow with it (ADR-0057).
    int pattern = static_cast<int>(light.animation.pattern);
    if (ImGui::Combo("Animation", &pattern, "Steady\0Flicker\0Buzz\0Failing\0Strobe\0Pulse\0")) {
        light.animation.pattern = static_cast<scene::LightPattern>(pattern);
        changed = true;
    }
    if (light.animation.pattern != scene::LightPattern::Steady) {
        changed |= ImGui::DragFloat("Rate", &light.animation.rate, 0.01f, 0.0f, 20.0f, "%.2f");
        changed |= ImGui::SliderFloat("Depth", &light.animation.depth, 0.0f, 1.0f);
    }
    changed |= InputString("Power circuit", light.animation.circuit);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Game code can cut a circuit: its lights stutter out, and blink back on");
    }
    return changed;
}

bool InspectWet(scene::WetVolume& wet) {
    bool changed = ImGui::Checkbox("Enabled", &wet.enabled);
    changed |= ImGui::SliderFloat("Wetness", &wet.wetness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Puddles", &wet.puddles, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Water height (m)", &wet.puddleHeight, 0.005f, 0.0f, 20.0f, "%.3f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Above the box's bottom: floors below it hold puddles");
    }
    changed |= ImGui::DragFloat("Rain (drops/m2/s)", &wet.rain, 0.5f, 0.0f, 500.0f, "%.1f");
    changed |= ImGui::SliderFloat("Streaks on walls", &wet.streaks, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Drying (per minute)", &wet.dryingRate, 0.01f, 0.0f, 10.0f, "%.2f");
    return changed;
}

/// An optional number: a box to set it, and the value from elsewhere while it is not.
bool OptionalFloat(const char* label, std::optional<float>& value, float fallback, float most) {
    bool set = value.has_value();
    bool changed = false;
    ImGui::PushID(label);
    if (ImGui::Checkbox("##set", &set)) {
        value = set ? std::optional<float>(fallback) : std::nullopt;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!set);
    float shown = value.value_or(fallback);
    if (ImGui::DragFloat(label, &shown, 0.01f, 0.0f, most, "%.2f") && set) {
        value = shown;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

/// The joints of a loose object (ADR-0126): each with its kind, what it holds to, where, and how it limits, drives
/// and breaks. Hinge angles show in degrees.
bool InspectJoints(std::vector<scene::LevelJoint>& joints) {
    if (joints.empty() || !ImGui::CollapsingHeader("Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }
    bool changed = false;
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < joints.size(); ++index) {
        scene::LevelJoint& joint = joints[index];
        ImGui::PushID(static_cast<int>(index));
        const bool hinge = joint.kind == physics::JointKind::Hinge;
        const bool slider = joint.kind == physics::JointKind::Slider;
        const bool distance = joint.kind == physics::JointKind::Distance;
        const std::string title = std::format("{} {}", index + 1, scene::JointKindName(joint.kind));
        if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            int kind = static_cast<int>(joint.kind);
            if (ImGui::Combo("Kind", &kind, "Point (ball)\0Hinge\0Distance (rope)\0Fixed (glue)\0Slider\0")) {
                joint.kind = static_cast<physics::JointKind>(kind);
                changed = true;
            }
            changed |= ImGui::InputScalar("Holds to (id)", ImGuiDataType_U64, &joint.to);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The id of the loose object or static box it holds to; 0: the world");
            }
            changed |= ImGui::DragFloat3("Anchor (m)", &joint.anchor.x, 0.01f, -20.0f, 20.0f, "%.2f");
            if (distance) {
                changed |= ImGui::DragFloat3("Other end (m)", &joint.toAnchor.x, 0.01f, -100.0f, 100.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("In the frame of what it holds to, or the world's");
                }
            }
            if (hinge || slider) {
                changed |= ImGui::DragFloat3("Axis", &joint.axis.x, 0.01f, -1.0f, 1.0f, "%.2f");
            }
            if (hinge || slider || distance) {
                changed |= ImGui::Checkbox("Limits", &joint.limited);
                if (joint.limited) {
                    float range[2] = {hinge ? glm::degrees(joint.lowest) : joint.lowest,
                                      hinge ? glm::degrees(joint.highest) : joint.highest};
                    if (ImGui::DragFloat2(hinge ? "Lowest, highest (deg)" : "Lowest, highest (m)", range,
                                          hinge ? 0.5f : 0.01f, hinge ? -180.0f : -50.0f, hinge ? 180.0f : 50.0f,
                                          "%.2f")) {
                        joint.lowest = hinge ? glm::radians(range[0]) : range[0];
                        joint.highest =
                            hinge ? glm::radians(std::max(range[0], range[1])) : std::max(range[0], range[1]);
                        changed = true;
                    }
                }
            }
            if (hinge || slider) {
                changed |= ImGui::DragFloat(hinge ? "Friction (N m)" : "Friction (N)", &joint.friction, 0.5f, 0.0f,
                                            10000.0f, "%.1f");
                int mode = static_cast<int>(joint.motor.mode);
                if (ImGui::Combo("Motor", &mode, "Off\0Speed\0Position\0")) {
                    joint.motor.mode = static_cast<physics::MotorMode>(mode);
                    changed = true;
                }
                if (joint.motor.mode != physics::MotorMode::Off) {
                    const bool speed = joint.motor.mode == physics::MotorMode::Speed;
                    float target = hinge ? glm::degrees(joint.motor.target) : joint.motor.target;
                    const char* label =
                        hinge ? (speed ? "Target (deg/s)" : "Target (deg)") : (speed ? "Target (m/s)" : "Target (m)");
                    if (ImGui::DragFloat(label, &target, hinge ? 1.0f : 0.01f)) {
                        joint.motor.target = hinge ? glm::radians(target) : target;
                        changed = true;
                    }
                    changed |= ImGui::DragFloat(hinge ? "Strength (N m)" : "Strength (N)", &joint.motor.strength, 10.0f,
                                                0.0f, 1.0e6f, "%.0f");
                }
            }
            changed |= ImGui::DragFloat("Breaks at (N)", &joint.breakForce, 10.0f, 0.0f, 1.0e6f, "%.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Pulled harder than this, it breaks for good; 0 never breaks. A body's weight is "
                                  "about 10 N a kilogram");
            }
            if (ImGui::SmallButton("Remove this joint")) {
                removed = index;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removed.has_value()) {
        joints.erase(joints.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed = true;
    }
    return changed;
}

bool InspectBody(scene::Body& body, const scene::Model* model) {
    bool changed = false;
    int shape = static_cast<int>(body.shape);
    if (ImGui::Combo("Shape", &shape, "Box\0Sphere\0Cylinder\0Capsule\0Hull of the model\0")) {
        body.shape = static_cast<physics::ShapeKind>(shape);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A sphere, cylinder or capsule fits in the size; cylinders and capsules stand along Y. "
                          "A hull wraps the model.");
    }
    if (body.shape != physics::ShapeKind::Hull) {
        changed |= ImGui::DragFloat3("Size (m)", &body.size.x, 0.01f, 0.02f, 20.0f, "%.2f");
        changed |= ImGui::DragFloat3("Center (m)", &body.center.x, 0.01f, -20.0f, 20.0f, "%.2f");
        if (model != nullptr) {
            if (ImGui::Button("Fit to the model")) {
                const std::vector<glm::vec3> points = scene::ModelPoints(*model);
                if (!points.empty()) {
                    glm::vec3 low = points.front();
                    glm::vec3 high = points.front();
                    for (const glm::vec3& point : points) {
                        low = glm::min(low, point);
                        high = glm::max(high, point);
                    }
                    body.size = glm::max(high - low, glm::vec3(0.02f));
                    body.center = (low + high) * 0.5f;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Size and center from the model's bounds");
            }
        }
    }
    changed |= ImGui::DragFloat("Mass (kg)", &body.mass, 0.5f, 0.0f, 5000.0f, "%.1f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 makes it static. Up to 40 kg, the player can pick it up and throw it");
    }
    // What it is made of: the model's main surface unless chosen.
    const std::string modelSurface = model != nullptr ? scene::MainSurface(*model) : std::string{};
    const std::string shown = body.surface.empty() ? (modelSurface.empty() ? std::string("model's (none: wood)")
                                                                           : "model's (" + modelSurface + ")")
                                                   : body.surface;
    if (ImGui::BeginCombo("Surface", shown.c_str())) {
        if (ImGui::Selectable("model's", body.surface.empty())) {
            body.surface.clear();
            changed = true;
        }
        for (const std::string_view surface : scene::kBodySurfaces) {
            if (ImGui::Selectable(std::string(surface).c_str(), body.surface == surface)) {
                body.surface = surface;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    const physics::BodyMaterial material =
        scene::SurfaceMaterial(body.surface.empty() ? std::string_view(modelSurface) : body.surface);
    changed |= OptionalFloat("Friction", body.friction, material.friction, 2.0f);
    changed |= OptionalFloat("Bounce", body.restitution, material.restitution, 1.0f);
    if (body.mass > 0.0f) {
        changed |= ImGui::Checkbox("Fast (debris)", &body.fast);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Swept each step, so that it does not fly through thin walls");
        }
        changed |= ImGui::Checkbox("Held until released", &body.held);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stays where it is until a sequence's release or impulse cue lets it go");
        }
        changed |= InputString("Group", body.group);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Loose objects of the same group never touch one another: a chain and its load");
        }
    } else {
        changed |= ImGui::DragFloat3("Conveyor (m/s)", &body.conveyor.x, 0.01f, -10.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("What lies on it is carried at this velocity, in the entity's frame");
        }
    }
    return changed;
}

bool InspectDecal(scene::Decal& decal) {
    bool changed = ImGui::Checkbox("Enabled", &decal.enabled);
    changed |= InputString("Texture", decal.texture);
    changed |= InputString("Normal map", decal.normal);
    changed |= ImGui::ColorEdit3("Color", &decal.color.x);
    changed |= ImGui::SliderFloat("Opacity", &decal.opacity, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Roughness", &decal.roughness, 0.045f, 1.0f);
    changed |= ImGui::SliderFloat("Metallic", &decal.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Angle fade", &decal.angleFade, -1.0f, 0.95f);
    ImGui::TextDisabled("The box projects along its -Z: scale it to the size of the mark");
    return changed;
}

bool InspectVisibility(scene::RayVisibility& visibility) {
    bool changed = ImGui::Checkbox("Camera", &visibility.camera);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Shadows", &visibility.shadows);
    changed |= ImGui::Checkbox("Reflections", &visibility.reflections);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Bounced light", &visibility.indirect);
    ImGui::TextDisabled("Which rays see its meshes and those under it");
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

bool InspectTrigger(scene::Trigger& trigger) {
    bool changed = ImGui::Checkbox("Enabled", &trigger.enabled);
    changed |= InputString("Sequence", trigger.sequence);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A .sequence.json file, relative to the level: what plays when the player walks in");
    }
    changed |= ImGui::Checkbox("Once", &trigger.once);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: it plays again each time the player comes back in");
    }
    return changed;
}

/// An ambient sound's clip and how far it is heard (ADR-0116).
bool InspectSound(scene::LevelSound& sound) {
    bool changed = InputString("Clip", sound.clip);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A .wav or .ogg file, relative to the level's file; drag one from Assets");
    }
    changed |= ImGui::SliderFloat("Volume", &sound.volume, 0.0f, 2.0f);
    changed |= ImGui::DragFloat("Full within (m)", &sound.minDistance, 0.05f, 0.1f, 100.0f, "%.2f");
    changed |= ImGui::DragFloat("Fades out at (m)", &sound.maxDistance, 0.1f, 0.5f, 500.0f, "%.1f");
    changed |= ImGui::Checkbox("Fades with its light", &sound.followsLight);
    ImGui::TextDisabled("Show > Hear the level plays it in the editor.");
    return changed;
}

/// A block's shape, size and color (ADR-0115).
bool InspectBlock(scene::LevelBlock& block) {
    bool changed = false;
    constexpr std::array<const char*, 4> kShapes{"Box", "Stairs", "Ramp", "Cylinder"};
    int shape = static_cast<int>(block.shape);
    if (ImGui::Combo("Shape", &shape, kShapes.data(), static_cast<int>(kShapes.size()))) {
        block.shape = static_cast<scene::BlockShape>(shape);
        changed = true;
    }
    changed |= ImGui::DragFloat3("Size (m)", &block.size.x, 0.05f, 0.05f, 200.0f, "%.2f");
    if (block.shape == scene::BlockShape::Stairs) {
        int steps = static_cast<int>(block.steps);
        if (ImGui::SliderInt("Steps", &steps, 1, 64)) {
            block.steps = static_cast<std::uint32_t>(steps);
            changed = true;
        }
        ImGui::TextDisabled("Each step is %.2f m high, %.2f m deep", block.size.y / static_cast<float>(block.steps),
                            block.size.z / static_cast<float>(block.steps));
    }
    if (block.shape == scene::BlockShape::Cylinder) {
        int sides = static_cast<int>(block.sides);
        if (ImGui::SliderInt("Sides", &sides, 3, 128)) {
            block.sides = static_cast<std::uint32_t>(sides);
            changed = true;
        }
    }
    changed |= ImGui::ColorEdit3("Color", &block.color.x);
    ImGui::TextDisabled("Stands on its origin; stairs and ramps rise towards -Z. The game walks on it.");
    return changed;
}

bool InspectComponents(scene::LevelEntity& entity, const scene::Model* model) {
    bool changed = false;
    changed |= Section("Block", entity.block, InspectBlock);
    changed |= Section("Sound", entity.sound, InspectSound);
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
    changed |= Section("Ray visibility", entity.visibility, InspectVisibility);
    changed |= Section("Decal", entity.decal, InspectDecal);
    changed |= Section("Wet", entity.wet, InspectWet);
    changed |= Section("Visibility cell", entity.cell, [](scene::VisibilityCell& cell) {
        const bool edited = ImGui::Checkbox("Enabled", &cell.enabled);
        ImGui::TextDisabled(
            "A room or a doorway (ADR-0089): where two cells' boxes overlap is the opening between them.");
        return edited;
    });
    changed |= Section("Trigger", entity.trigger, InspectTrigger);
    changed |= Section("Loose object", entity.body, [model](scene::Body& body) { return InspectBody(body, model); });
    changed |= InspectJoints(entity.joints);

    ImGui::Separator();
    if (ImGui::Button("Add a component")) {
        ImGui::OpenPopup("add component");
    }
    if (ImGui::BeginPopup("add component")) {
        if (!entity.sound && ImGui::MenuItem("Sound")) {
            entity.sound = scene::LevelSound{};
            changed = true;
        }
        if (!entity.block && ImGui::MenuItem("Block")) {
            entity.block = scene::LevelBlock{};
            changed = true;
        }
        if (!entity.body && ImGui::MenuItem("Loose object")) {
            entity.body = scene::Body{};
            changed = true;
        }
        if (entity.body && entity.body->mass > 0.0f && ImGui::MenuItem("Joint")) {
            entity.joints.emplace_back();
            changed = true;
        }
        if (!entity.trigger && ImGui::MenuItem("Trigger")) {
            entity.trigger = scene::Trigger{};
            changed = true;
        }
        if (!entity.cell && ImGui::MenuItem("Visibility cell")) {
            entity.cell = scene::VisibilityCell{};
            changed = true;
        }
        if (!entity.wet && ImGui::MenuItem("Wet")) {
            entity.wet = scene::WetVolume{};
            changed = true;
        }
        if (!entity.decal && ImGui::MenuItem("Decal")) {
            entity.decal = scene::Decal{.texture = "Decals/grime.png"};
            changed = true;
        }
        if (!entity.visibility && ImGui::MenuItem("Ray visibility")) {
            entity.visibility = scene::RayVisibility{};
            changed = true;
        }
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
    if (entity.sound) {
        return "S";
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
    if (entity.block) {
        return "B";
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
    if (entity.sound) {
        return IM_COL32(150, 120, 240, 255);
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
