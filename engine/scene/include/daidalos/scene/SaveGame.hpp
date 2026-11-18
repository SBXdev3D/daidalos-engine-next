// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/World.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::scene {

class Scene;

/// ECS component: the name a saved game knows an entity by (ADR-0072), given when the level is made, before
/// anything changes, so that it stays the same whatever is taken or destroyed. Level entities are known by
/// their LevelEntityId and need none.
struct SaveKey {
    std::string value;
};

/// A saved game (ADR-0072): values by key, a line each, "key = value", which reads and diffs as text. Keys are
/// dotted paths: "player.health", "entity.42.position".
class SaveData {
public:
    void Put(std::string_view key, double value);
    void Put(std::string_view key, bool value);
    void Put(std::string_view key, std::string_view value);
    /// Text, not a flag: a pointer would otherwise turn into a bool.
    void Put(std::string_view key, const char* value) { Put(key, std::string_view(value)); }
    void Put(std::string_view key, const glm::vec3& value);
    void Put(std::string_view key, const glm::quat& value);

    [[nodiscard]] std::optional<double> Number(std::string_view key) const;
    [[nodiscard]] std::optional<bool> Flag(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> Text(std::string_view key) const;
    [[nodiscard]] std::optional<glm::vec3> Vector(std::string_view key) const;
    [[nodiscard]] std::optional<glm::quat> Rotation(std::string_view key) const;
    [[nodiscard]] bool Has(std::string_view key) const;
    /// The keys that start with a prefix, in order.
    [[nodiscard]] std::vector<std::string> Keys(std::string_view prefix = {}) const;

    [[nodiscard]] std::string Write() const;
    [[nodiscard]] static core::Result<SaveData> Parse(std::string_view text);

    bool operator==(const SaveData&) const = default;

private:
    std::map<std::string, std::string, std::less<>> m_values;
};

/// Writes a save whole, through a file beside it, so that a crash never leaves half of one.
[[nodiscard]] core::Result<void> WriteSave(const std::filesystem::path& file, const SaveData& data);
[[nodiscard]] core::Result<SaveData> ReadSave(const std::filesystem::path& file);

/// What the engine keeps of a world (ADR-0072): where each level entity and keyed entity is, which lights are
/// on, what rays see, which triggers have fired, how wet the wet places are, and the power circuits. The game
/// adds its own under its own keys.
void SaveScene(Scene& scene, SaveData& data);
/// Puts back what SaveScene kept, into a world made from the same level.
void LoadScene(Scene& scene, const SaveData& data);

} // namespace daidalos::scene
