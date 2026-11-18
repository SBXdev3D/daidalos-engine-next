// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/SaveGame.hpp>
#include <daidalos/scene/Scene.hpp>

#include <charconv>
#include <format>
#include <fstream>
#include <sstream>

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";
constexpr std::string_view kHeader = "daidalos-save 1";

std::optional<std::vector<double>> Numbers(std::string_view text) {
    std::vector<double> numbers;
    while (!text.empty()) {
        while (!text.empty() && text.front() == ' ') {
            text.remove_prefix(1);
        }
        if (text.empty()) {
            break;
        }
        double value = 0.0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        numbers.push_back(value);
        text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    }
    return numbers;
}

// The key every saved entity goes by: its level id, or its save key.
std::optional<std::string> EntityKey(ecs::World& world, ecs::Entity entity) {
    if (const SaveKey* key = world.TryGet<SaveKey>(entity)) {
        return "keyed." + key->value;
    }
    if (const LevelEntityId* id = world.TryGet<LevelEntityId>(entity)) {
        return std::format("entity.{}", id->value);
    }
    return std::nullopt;
}

} // namespace

void SaveData::Put(std::string_view key, double value) {
    m_values.insert_or_assign(std::string(key), std::format("{}", value));
}

void SaveData::Put(std::string_view key, bool value) {
    m_values.insert_or_assign(std::string(key), value ? "true" : "false");
}

void SaveData::Put(std::string_view key, std::string_view value) {
    // Text is quoted, so that it is never read as a number; a line holds it whole.
    std::string quoted = "\"";
    for (const char c : value) {
        quoted += c == '\n' ? ' ' : c;
    }
    m_values.insert_or_assign(std::string(key), quoted + "\"");
}

void SaveData::Put(std::string_view key, const glm::vec3& value) {
    m_values.insert_or_assign(std::string(key), std::format("{} {} {}", value.x, value.y, value.z));
}

void SaveData::Put(std::string_view key, const glm::quat& value) {
    m_values.insert_or_assign(std::string(key), std::format("{} {} {} {}", value.w, value.x, value.y, value.z));
}

std::optional<double> SaveData::Number(std::string_view key) const {
    const auto found = m_values.find(key);
    if (found == m_values.end()) {
        return std::nullopt;
    }
    const std::optional<std::vector<double>> numbers = Numbers(found->second);
    return numbers.has_value() && numbers->size() == 1 ? std::optional<double>(numbers->front()) : std::nullopt;
}

std::optional<bool> SaveData::Flag(std::string_view key) const {
    const auto found = m_values.find(key);
    if (found == m_values.end() || (found->second != "true" && found->second != "false")) {
        return std::nullopt;
    }
    return found->second == "true";
}

std::optional<std::string> SaveData::Text(std::string_view key) const {
    const auto found = m_values.find(key);
    if (found == m_values.end() || found->second.size() < 2 || found->second.front() != '"' ||
        found->second.back() != '"') {
        return std::nullopt;
    }
    return found->second.substr(1, found->second.size() - 2);
}

std::optional<glm::vec3> SaveData::Vector(std::string_view key) const {
    const auto found = m_values.find(key);
    if (found == m_values.end()) {
        return std::nullopt;
    }
    const std::optional<std::vector<double>> numbers = Numbers(found->second);
    if (!numbers.has_value() || numbers->size() != 3) {
        return std::nullopt;
    }
    return glm::vec3((*numbers)[0], (*numbers)[1], (*numbers)[2]);
}

std::optional<glm::quat> SaveData::Rotation(std::string_view key) const {
    const auto found = m_values.find(key);
    if (found == m_values.end()) {
        return std::nullopt;
    }
    const std::optional<std::vector<double>> numbers = Numbers(found->second);
    if (!numbers.has_value() || numbers->size() != 4) {
        return std::nullopt;
    }
    return glm::quat(static_cast<float>((*numbers)[0]), static_cast<float>((*numbers)[1]),
                     static_cast<float>((*numbers)[2]), static_cast<float>((*numbers)[3]));
}

bool SaveData::Has(std::string_view key) const {
    return m_values.contains(key);
}

std::vector<std::string> SaveData::Keys(std::string_view prefix) const {
    std::vector<std::string> keys;
    for (auto found = m_values.lower_bound(prefix); found != m_values.end() && found->first.starts_with(prefix);
         ++found) {
        keys.push_back(found->first);
    }
    return keys;
}

std::string SaveData::Write() const {
    std::string text = std::string(kHeader) + "\n";
    for (const auto& [key, value] : m_values) {
        text += std::format("{} = {}\n", key, value);
    }
    return text;
}

core::Result<SaveData> SaveData::Parse(std::string_view text) {
    SaveData data;
    std::size_t lineNumber = 0;
    bool headed = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        if (!headed) {
            if (line != kHeader) {
                return core::MakeError(core::ErrorCode::ParseError, kModule, "Not a saved game");
            }
            headed = true;
            continue;
        }
        const std::size_t equals = line.find(" = ");
        if (equals == std::string_view::npos || equals == 0) {
            return core::MakeError(core::ErrorCode::ParseError, kModule,
                                   std::format("Saved game, line {}: not \"key = value\"", lineNumber));
        }
        data.m_values.insert_or_assign(std::string(line.substr(0, equals)), std::string(line.substr(equals + 3)));
    }
    if (!headed) {
        return core::MakeError(core::ErrorCode::ParseError, kModule, "Not a saved game");
    }
    return data;
}

core::Result<void> WriteSave(const std::filesystem::path& file, const SaveData& data) {
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    std::filesystem::path temporary = file;
    temporary += ".writing";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << data.Write();
        if (!stream) {
            return core::MakeError(core::ErrorCode::IoError, kModule,
                                   std::format("Cannot write '{}'", temporary.generic_string()));
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot replace '{}': {}", file.generic_string(), error.message()));
    }
    return {};
}

core::Result<SaveData> ReadSave(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return core::MakeError(core::ErrorCode::NotFound, kModule,
                               std::format("No saved game at '{}'", file.generic_string()));
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return SaveData::Parse(buffer.str());
}

void SaveScene(Scene& scene, SaveData& data) {
    ecs::World& world = scene.World();
    world.Each<ecs::LocalTransform>([&](ecs::Entity entity, const ecs::LocalTransform& transform) {
        const std::optional<std::string> key = EntityKey(world, entity);
        if (!key.has_value()) {
            return;
        }
        data.Put(*key + ".position", transform.translation);
        data.Put(*key + ".rotation", transform.rotation);
        if (const Light* light = world.TryGet<Light>(entity)) {
            data.Put(*key + ".light", light->enabled);
        }
        if (const RayVisibility* visibility = world.TryGet<RayVisibility>(entity)) {
            data.Put(*key + ".seen", visibility->camera);
        }
        if (const TriggerState* trigger = world.TryGet<TriggerState>(entity)) {
            data.Put(*key + ".fired", trigger->fired);
        }
        if (const WetVolume* wet = world.TryGet<WetVolume>(entity)) {
            data.Put(*key + ".wetness", static_cast<double>(wet->wetness));
        }
    });
    for (const auto& [circuit, on] : scene.Power().Switched()) {
        data.Put("power." + circuit, on);
    }
}

void LoadScene(Scene& scene, const SaveData& data) {
    ecs::World& world = scene.World();
    std::vector<ecs::Entity> entities;
    world.Each<ecs::LocalTransform>(
        [&](ecs::Entity entity, const ecs::LocalTransform&) { entities.push_back(entity); });
    for (const ecs::Entity entity : entities) {
        const std::optional<std::string> key = EntityKey(world, entity);
        if (!key.has_value()) {
            continue;
        }
        ecs::LocalTransform& transform = world.Get<ecs::LocalTransform>(entity);
        transform.translation = data.Vector(*key + ".position").value_or(transform.translation);
        transform.rotation = data.Rotation(*key + ".rotation").value_or(transform.rotation);
        if (Light* light = world.TryGet<Light>(entity)) {
            light->enabled = data.Flag(*key + ".light").value_or(light->enabled);
        }
        if (const std::optional<bool> seen = data.Flag(*key + ".seen")) {
            RayVisibility& visibility =
                world.Has<RayVisibility>(entity) ? world.Get<RayVisibility>(entity) : world.Add<RayVisibility>(entity);
            visibility = {*seen, *seen, *seen, *seen};
        }
        if (const std::optional<bool> fired = data.Flag(*key + ".fired")) {
            TriggerState& state =
                world.Has<TriggerState>(entity) ? world.Get<TriggerState>(entity) : world.Add<TriggerState>(entity);
            state.fired = *fired;
        }
        if (WetVolume* wet = world.TryGet<WetVolume>(entity)) {
            wet->wetness = static_cast<float>(data.Number(*key + ".wetness").value_or(wet->wetness));
        }
    }
    for (const std::string& key : data.Keys("power.")) {
        scene.Power().Set(key.substr(6), data.Flag(key).value_or(true), scene.LightClock());
    }
    ecs::PropagateTransforms(world);
    scene.CutView();
}

} // namespace daidalos::scene
