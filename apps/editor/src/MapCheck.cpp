// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "MapCheck.hpp"

#include <daidalos/math/Transform.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <unordered_map>

using namespace daidalos;

namespace editor {

namespace {

std::string Label(const scene::LevelEntity& entity) {
    return entity.name.empty() ? std::format("Entity {}", entity.id) : entity.name;
}

/// The entity's matrix in the world, through its parents; identity where the chain breaks.
glm::mat4 WorldOf(const scene::Level& level, const scene::LevelEntity& entity) {
    glm::mat4 world = math::ToMatrix(entity.transform);
    std::uint64_t parent = entity.parent;
    for (std::size_t steps = 0; parent != 0 && steps <= level.entities.size(); ++steps) {
        const scene::LevelEntity* above = level.Find(parent);
        if (above == nullptr) {
            break;
        }
        world = math::ToMatrix(above->transform) * world;
        parent = above->parent;
    }
    return world;
}

/// Sequence cues that name an entity of the level.
bool NamesEntity(scene::Cue::Kind kind) {
    switch (kind) {
    case scene::Cue::Kind::Light:
    case scene::Cue::Kind::Move:
    case scene::Cue::Kind::Visible:
    case scene::Cue::Kind::Blast:
    case scene::Cue::Kind::Impulse:
    case scene::Cue::Kind::Release:
    case scene::Cue::Kind::Break:
    case scene::Cue::Kind::Motor:
    case scene::Cue::Kind::Shot:
        return true;
    default:
        return false;
    }
}

} // namespace

std::string UnderRoot(const std::string& levelPath, const std::string& path) {
    return (std::filesystem::path(levelPath).parent_path() / path).lexically_normal().generic_string();
}

std::vector<Problem> CheckLevel(const scene::Level& level, const std::string& levelPath, const CheckSources& sources) {
    std::vector<Problem> problems;
    const auto error = [&](std::uint64_t entity, std::string message) {
        problems.push_back({Problem::Severity::Error, entity, std::move(message)});
    };
    const auto warning = [&](std::uint64_t entity, std::string message) {
        problems.push_back({Problem::Severity::Warning, entity, std::move(message)});
    };
    const auto note = [&](std::uint64_t entity, std::string message) {
        problems.push_back({Problem::Severity::Note, entity, std::move(message)});
    };
    const auto file = [&](std::uint64_t entity, const std::string& what, const std::string& path) {
        if (!path.empty() && sources.exists && !sources.exists(UnderRoot(levelPath, path))) {
            error(entity, std::format("{} not found: {}", what, UnderRoot(levelPath, path)));
        }
    };

    // Ids and parents.
    std::map<std::uint64_t, std::size_t> ids;
    std::set<std::string> names;
    for (const scene::LevelEntity& entity : level.entities) {
        if (++ids[entity.id] == 2) {
            error(entity.id, std::format("The id {} is used by more than one entity", entity.id));
        }
        names.insert(entity.name);
    }
    for (const scene::LevelEntity& entity : level.entities) {
        if (entity.parent != 0 && !ids.contains(entity.parent)) {
            error(entity.id,
                  std::format("{} hangs from entity {}, which is not in the level", Label(entity), entity.parent));
        }
        std::uint64_t parent = entity.parent;
        for (std::size_t steps = 0; parent != 0; ++steps) {
            if (parent == entity.id || steps > level.entities.size()) {
                error(entity.id, std::format("{} hangs, through its parents, from itself", Label(entity)));
                break;
            }
            const scene::LevelEntity* above = level.Find(parent);
            parent = above != nullptr ? above->parent : 0;
        }
    }

    // Files.
    if (!level.environment.skyFaces.empty()) {
        file(0, "The sky's front face", level.environment.skyFaces + "_front.png");
    }
    for (const scene::LevelEntity& entity : level.entities) {
        file(entity.id, std::format("{}'s model", Label(entity)), entity.model);
        file(entity.id, std::format("{}'s prefab", Label(entity)), entity.prefab);
        if (entity.particles) {
            file(entity.id, std::format("{}'s effect", Label(entity)), entity.particles->effect);
        }
        if (entity.attractor) {
            file(entity.id, std::format("{}'s effect", Label(entity)), entity.attractor->effect);
        }
        if (entity.weather) {
            file(entity.id, std::format("{}'s effect", Label(entity)), entity.weather->effect);
        }
        if (entity.ribbon) {
            file(entity.id, std::format("{}'s ribbon", Label(entity)), entity.ribbon->effect);
        }
        if (entity.sound) {
            file(entity.id, std::format("{}'s sound", Label(entity)), entity.sound->clip);
        }
        if (entity.decal) {
            file(entity.id, std::format("{}'s image", Label(entity)), entity.decal->texture);
            file(entity.id, std::format("{}'s normal map", Label(entity)), entity.decal->normal);
        }
        if (entity.light) {
            file(entity.id, std::format("{}'s profile", Label(entity)), entity.light->profile);
            file(entity.id, std::format("{}'s cookie", Label(entity)), entity.light->cookie);
            if (entity.light->intensity <= 0.0f || entity.light->range <= 0.0f) {
                warning(entity.id, std::format("{} gives no light: its intensity or its range is 0", Label(entity)));
            }
        }
        if (entity.trigger) {
            if (entity.trigger->sequence.empty()) {
                warning(entity.id, std::format("{} plays no sequence", Label(entity)));
            } else {
                const std::string path = UnderRoot(levelPath, entity.trigger->sequence);
                if (sources.exists && !sources.exists(path)) {
                    error(entity.id, std::format("{}'s sequence not found: {}", Label(entity), path));
                } else if (sources.sequence) {
                    if (const std::optional<scene::Sequence> sequence = sources.sequence(path)) {
                        std::set<std::string> missing;
                        for (const scene::Cue& cue : sequence->cues) {
                            if (NamesEntity(cue.kind) && !cue.name.empty() && !names.contains(cue.name)) {
                                missing.insert(cue.name);
                            }
                        }
                        for (const std::string& name : missing) {
                            warning(entity.id, std::format("{}'s sequence names \"{}\", which the level has not",
                                                           Label(entity), name));
                        }
                    } else {
                        error(entity.id, std::format("{}'s sequence cannot be read: {}", Label(entity), path));
                    }
                }
            }
        }
        if (entity.transform.scale.x == 0.0f || entity.transform.scale.y == 0.0f || entity.transform.scale.z == 0.0f) {
            error(entity.id, std::format("{} has a scale of 0: it cannot be seen or moved", Label(entity)));
        }
    }

    // Copies of a model in the same place, as a paste or a duplicate left where it was.
    std::map<std::pair<std::string, std::uint64_t>, std::vector<const scene::LevelEntity*>> byModel;
    for (const scene::LevelEntity& entity : level.entities) {
        const std::string& placed = !entity.model.empty() ? entity.model : entity.prefab;
        if (!placed.empty()) {
            byModel[{placed, entity.parent}].push_back(&entity);
        }
    }
    for (const auto& [key, entities] : byModel) {
        for (std::size_t first = 0; first < entities.size(); ++first) {
            for (std::size_t second = first + 1; second < entities.size(); ++second) {
                const math::Transform& a = entities[first]->transform;
                const math::Transform& b = entities[second]->transform;
                if (glm::distance(a.translation, b.translation) < 0.001f &&
                    std::abs(glm::dot(a.rotation, b.rotation)) > 0.99999f && glm::distance(a.scale, b.scale) < 0.001f) {
                    warning(entities[second]->id, std::format("{} lies exactly on {}: a copy left in place?",
                                                              Label(*entities[second]), Label(*entities[first])));
                }
            }
        }
    }

    // The player's start.
    std::vector<std::uint64_t> starts;
    for (const scene::LevelEntity& entity : level.entities) {
        if (entity.game && entity.game->kind == "player") {
            starts.push_back(entity.id);
        }
    }
    if (starts.empty()) {
        warning(0, "No player start: the game starts the player at the origin");
    }
    for (std::size_t extra = 1; extra < starts.size(); ++extra) {
        error(starts[extra], "More than one player start: the game takes one of them");
    }

    // With rooms, what lies outside every room is drawn from everywhere or nowhere (ADR-0089).
    std::vector<std::pair<glm::vec3, glm::vec3>> rooms;
    for (const scene::LevelEntity& entity : level.entities) {
        if (entity.cell && entity.cell->enabled) {
            const glm::mat4 world = WorldOf(level, entity);
            glm::vec3 low(1e30f);
            glm::vec3 high(-1e30f);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1) != 0 ? 0.5f : -0.5f, (corner & 2) != 0 ? 0.5f : -0.5f,
                                      (corner & 4) != 0 ? 0.5f : -0.5f);
                const glm::vec3 at = math::TransformPoint(world, local);
                low = glm::min(low, at);
                high = glm::max(high, at);
            }
            rooms.emplace_back(low, high);
        }
    }
    if (!rooms.empty()) {
        for (const scene::LevelEntity& entity : level.entities) {
            const bool placed = !entity.model.empty() || !entity.prefab.empty() || entity.light || entity.particles;
            if (!placed || entity.cell) {
                continue;
            }
            const glm::vec3 at(WorldOf(level, entity)[3]);
            const bool inside = std::ranges::any_of(rooms, [&at](const auto& room) {
                return glm::all(glm::greaterThanEqual(at, room.first)) && glm::all(glm::lessThanEqual(at, room.second));
            });
            if (!inside) {
                note(entity.id, std::format("{} is outside every room: drawn from everywhere", Label(entity)));
            }
        }
    }

    std::ranges::stable_sort(problems, [](const Problem& a, const Problem& b) { return a.severity < b.severity; });
    return problems;
}

} // namespace editor
