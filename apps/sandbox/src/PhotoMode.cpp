// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "PhotoMode.hpp"

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <variant>

#include "Combat.hpp"
#include "Controls.hpp"
#include "Factory.hpp"

namespace sandbox {

namespace {

using namespace daidalos;

constexpr std::array<float, 9> kApertures{0.0f, 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f};
/// Meters a second, and with Run held.
constexpr float kSpeed = 2.0f;
constexpr float kFastSpeed = 6.0f;
/// The camera stays this far from walls.
constexpr float kMargin = 0.2f;
/// Radians a second at full tilt of the right stick.
constexpr float kStickTurn = 1.8f;
constexpr float kMaxPitch = glm::radians(89.0f);
/// A 35 mm frame is 24 mm high: a focal length gives the vertical field of view.
constexpr float kFrameHeight = 24.0f;

std::string ValueText(const core::CVarValue& value) {
    return std::visit(
        [](const auto& typed) -> std::string {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, bool>) {
                return typed ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return typed;
            } else {
                return std::format("{}", typed);
            }
        },
        value);
}

} // namespace

void PhotoMode::Override(app::Application& app, std::string_view name, std::string_view value) {
    const core::CVarEntry* entry = app.CVars().Find(name);
    if (entry == nullptr) {
        return;
    }
    if (std::ranges::none_of(m_saved, [&](const auto& saved) { return saved.first == name; })) {
        m_saved.emplace_back(std::string(name), entry->set ? std::optional(ValueText(entry->value)) : std::nullopt);
    }
    if (const core::Result<void> set = app.CVars().Set(name, value); !set) {
        DAI_LOG_WARNING("sandbox", "Photo mode: {}", core::Format(set.error()));
    }
}

void PhotoMode::Enter(app::Application& app, scene::Scene& scene, ecs::Entity player) {
    ecs::World& world = scene.World();
    m_playerCamera = scene.ActiveCamera();
    const ecs::WorldTransform* eye =
        world.IsAlive(m_playerCamera) ? world.TryGet<ecs::WorldTransform>(m_playerCamera) : nullptr;
    m_origin = eye != nullptr ? glm::vec3(eye->matrix[3]) : glm::vec3(0.0f);
    m_position = m_origin;
    m_yaw = 0.0f;
    m_pitch = 0.0f;
    if (const Player* state = world.IsAlive(player) ? world.TryGet<Player>(player) : nullptr) {
        m_yaw = state->yaw;
        m_pitch = state->pitch;
        m_playerBody = scene.Physics().CharacterBody(state->character);
        HideWeapons(scene, player);
    }
    m_roll = 0.0f;
    m_camera = world.Create();
    world.Add<ecs::LocalTransform>(m_camera);
    world.Add<ecs::WorldTransform>(m_camera);
    world.Add<scene::Camera>(m_camera);
    scene.SetActiveCamera(m_camera);
    m_active = true;
    m_panelHidden = false;
    m_status.clear();
    // The photo is made at the window's full resolution, from the first frame.
    Override(app, "renderer.dynamic_resolution", "false");
    Override(app, "renderer.render_scale", "1");
    Override(app, "renderer.exposure_compensation", std::format("{:.2f}", m_exposure));
    Override(app, "renderer.film_grain", std::format("{:.2f}", m_grain));
    Override(app, "renderer.vignette", std::format("{:.2f}", m_vignette));
    Override(app, "renderer.debug_view", m_pathTraced ? "path" : "off");
    PlaceCamera(scene);
}

void PhotoMode::Leave(app::Application& app, scene::Scene& scene) {
    if (!m_active) {
        return;
    }
    scene.World().Destroy(m_camera);
    scene.SetActiveCamera(m_playerCamera);
    for (const auto& [name, value] : m_saved) {
        if (value.has_value()) {
            (void)app.CVars().Set(name, *value);
        } else {
            app.CVars().Reset(name);
        }
    }
    m_saved.clear();
    m_active = false;
}

void PhotoMode::PlaceCamera(scene::Scene& scene) {
    ecs::World& world = scene.World();
    if (!world.IsAlive(m_camera)) {
        return;
    }
    ecs::LocalTransform& transform = world.Get<ecs::LocalTransform>(m_camera);
    transform.translation = m_position;
    transform.rotation = math::FromYawPitchRoll(m_yaw, m_pitch, m_roll);
    scene::Camera& camera = world.Get<scene::Camera>(m_camera);
    camera.verticalFov = 2.0f * std::atan(kFrameHeight * 0.5f / m_focalLength);
    camera.aperture = kApertures.at(m_aperture);
    camera.focusDistance = m_focus;
    // The scene is paused, and its systems with it: the camera's world transform is made here.
    ecs::PropagateTransforms(world);
}

void PhotoMode::FocusAtCentre(scene::Scene& scene) {
    const glm::vec3 forward = math::FromYawPitchRoll(m_yaw, m_pitch, m_roll) * math::kForward;
    if (const std::optional<physics::RayHit> hit = scene.Physics().CastRay(m_position, forward, 200.0f, m_playerBody)) {
        m_focus = std::clamp(hit->distance, 0.3f, 50.0f);
    }
}

void PhotoMode::Update(app::Application& app, scene::Scene& scene, const input::ActionMap& actions, bool mouseCaptured,
                       float seconds) {
    if (!m_active) {
        return;
    }
    const input::InputState& input = app.Input();
    if (input.WasPressed(platform::Key::H)) {
        m_panelHidden = !m_panelHidden;
    }
    if (mouseCaptured) {
        m_yaw -= input.MouseDelta().x * app.MouseSensitivity();
        m_pitch -= input.MouseDelta().y * app.MouseSensitivity();
    }
    float rise = 0.0f;
    for (const platform::GamepadId gamepad : input.Gamepads()) {
        const auto stick = [&](platform::GamepadAxis axis) {
            const float value = input.Axis(gamepad, axis);
            return std::abs(value) < input::ActionMap::kDefaultDeadZone ? 0.0f : value;
        };
        m_yaw -= stick(platform::GamepadAxis::RightX) * kStickTurn * seconds;
        m_pitch += stick(platform::GamepadAxis::RightY) * kStickTurn * seconds;
        rise += stick(platform::GamepadAxis::RightTrigger) - stick(platform::GamepadAxis::LeftTrigger);
    }
    m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);

    rise += (actions.IsDown(input, Jump) ? 1.0f : 0.0f) - (actions.IsDown(input, Crouch) ? 1.0f : 0.0f);
    const glm::quat facing = math::FromYawPitchRoll(m_yaw, m_pitch, 0.0f);
    glm::vec3 move = (facing * math::kForward) * actions.Value(input, MoveForward) +
                     (facing * math::kRight) * actions.Value(input, MoveRight) +
                     math::kUp * std::clamp(rise, -1.0f, 1.0f);
    if (glm::length(move) > 1.0f) {
        move = glm::normalize(move);
    }
    const float speed = actions.IsDown(input, Run) ? kFastSpeed : kSpeed;
    glm::vec3 target = m_position + move * speed * seconds;
    // Within reach of the player's eyes.
    if (const glm::vec3 away = target - m_origin; glm::length(away) > kReach) {
        target = m_origin + glm::normalize(away) * kReach;
    }
    // Not through walls: the move stops short of what it would cross.
    if (const glm::vec3 step = target - m_position; glm::length(step) > 1e-5f) {
        const float length = glm::length(step);
        const glm::vec3 direction = step / length;
        if (const std::optional<physics::RayHit> hit =
                scene.Physics().CastRay(m_position, direction, length + kMargin, m_playerBody)) {
            target = m_position + direction * std::max(hit->distance - kMargin, 0.0f);
        }
    }
    m_position = target;
    m_statusSeconds = std::max(m_statusSeconds - seconds, 0.0f);
    for (const std::filesystem::path& saved : app.TakeSavedCaptures()) {
        m_status = std::filesystem::path(saved).filename().replace_extension().string();
        m_statusSeconds = 5.0f;
    }
    PlaceCamera(scene);
}

bool PhotoMode::Draw(ui::Context& ui, ui::Canvas& canvas, const HudAssets& hud, app::Application& app,
                     scene::Scene& scene) {
    if (!m_active) {
        return false;
    }
    const ui::TextStyle small{.size = 26.0f,
                              .color = {0.85f, 0.85f, 0.85f, 1.0f},
                              .horizontal = ui::Align::Center,
                              .vertical = ui::Align::Center,
                              .shadowColor = {0.0f, 0.0f, 0.0f, 0.8f}};
    if (m_statusSeconds > 0.0f) {
        canvas.DrawText(*hud.font, Tf(hud, "photo.saved", m_status), {canvas.Size().x * 0.5f, 60.0f}, small);
    }
    if (m_panelHidden) {
        return false;
    }
    constexpr float kWidth = 520.0f;
    constexpr float kRow = 44.0f;
    constexpr float kGap = 6.0f;
    const float left = canvas.Size().x - kWidth - 30.0f;
    float y = 40.0f;
    canvas.FillRect({{left - 16.0f, y - 16.0f}, {kWidth + 32.0f, 760.0f}}, {0.0f, 0.0f, 0.0f, 0.55f});
    canvas.DrawText(*hud.boldFont, T(hud, "photo.title"), {left + (kWidth * 0.5f), y + 22.0f},
                    {.size = 44.0f,
                     .color = {1.0f, 1.0f, 1.0f, 1.0f},
                     .horizontal = ui::Align::Center,
                     .vertical = ui::Align::Center});
    y += 64.0f;
    const auto row = [&]() {
        const ui::Rect rect{{left, y}, {kWidth, kRow}};
        y += kRow + kGap;
        return rect;
    };
    bool changed = false;
    changed |= ui.Slider(T(hud, "photo.focal_length"), row(), m_focalLength, 12.0f, 200.0f, 1.0f,
                         std::format("{:.0f} mm", m_focalLength));
    std::vector<std::string> apertureNames;
    apertureNames.emplace_back(T(hud, "photo.aperture.off"));
    for (std::size_t index = 1; index < kApertures.size(); ++index) {
        apertureNames.push_back(std::format("f/{:g}", kApertures[index]));
    }
    std::vector<std::string_view> apertureViews(apertureNames.begin(), apertureNames.end());
    changed |= ui.Choice(T(hud, "photo.aperture"), row(), m_aperture, apertureViews);
    changed |= ui.Slider(T(hud, "photo.focus"), row(), m_focus, 0.3f, 30.0f, 0.1f, std::format("{:.1f} m", m_focus));
    if (ui.Button(T(hud, "photo.focus_centre"), row())) {
        FocusAtCentre(scene);
        changed = true;
    }
    if (ui.Slider(T(hud, "photo.exposure"), row(), m_exposure, -3.0f, 3.0f, 0.1f,
                  std::format("{:+.1f} EV", m_exposure))) {
        Override(app, "renderer.exposure_compensation", std::format("{:.2f}", m_exposure));
    }
    changed |= ui.Slider(T(hud, "photo.roll"), row(), m_roll, glm::radians(-45.0f), glm::radians(45.0f),
                         glm::radians(1.0f), std::format("{:.0f}°", glm::degrees(m_roll)));
    if (ui.Slider(T(hud, "photo.grain"), row(), m_grain, 0.0f, 1.0f, 0.05f, std::format("{:.0f}%", m_grain * 100.0f))) {
        Override(app, "renderer.film_grain", std::format("{:.2f}", m_grain));
    }
    if (ui.Slider(T(hud, "photo.vignette"), row(), m_vignette, 0.0f, 1.0f, 0.05f,
                  std::format("{:.0f}%", m_vignette * 100.0f))) {
        Override(app, "renderer.vignette", std::format("{:.2f}", m_vignette));
    }
    if (ui.Toggle(T(hud, "photo.path_tracer"), row(), m_pathTraced)) {
        Override(app, "renderer.debug_view", m_pathTraced ? "path" : "off");
    }
    if (m_pathTraced) {
        canvas.DrawText(*hud.font, Tf(hud, "photo.paths", app.Renderer().Stats().pathSamples),
                        {left + (kWidth * 0.5f), y + (kRow * 0.5f)}, small);
    }
    y += kRow + kGap;
    if (ui.Button(T(hud, app.Capturing() ? "photo.saving" : "photo.capture"), row()) && !app.Capturing()) {
        app.CaptureFrame(app.NewCapturePath(), true);
    }
    const bool leave = ui.Button(T(hud, "photo.leave"), row());
    canvas.DrawText(*hud.font, T(hud, "photo.help"), {left + (kWidth * 0.5f), y + 14.0f}, small);
    if (changed) {
        PlaceCamera(scene);
    }
    return leave;
}

} // namespace sandbox
