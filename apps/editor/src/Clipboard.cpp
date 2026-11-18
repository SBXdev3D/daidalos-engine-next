// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Clipboard.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>

using namespace daidalos;

namespace editor {

std::vector<scene::LevelEntity> CopyEntities(const scene::Level& level, std::span<const std::uint64_t> selection) {
    std::set<std::uint64_t> taken(selection.begin(), selection.end());
    // Everything under a selected entity.
    for (bool grew = true; grew;) {
        grew = false;
        for (const scene::LevelEntity& entity : level.entities) {
            if (entity.parent != 0 && taken.contains(entity.parent) && taken.insert(entity.id).second) {
                grew = true;
            }
        }
    }
    // Parents first: the level's order, with an entity waiting until its parent, if copied, is out.
    std::vector<scene::LevelEntity> copies;
    std::set<std::uint64_t> placed;
    std::vector<const scene::LevelEntity*> waiting;
    for (const scene::LevelEntity& entity : level.entities) {
        if (taken.contains(entity.id)) {
            waiting.push_back(&entity);
        }
    }
    while (!waiting.empty()) {
        const std::size_t before = waiting.size();
        std::erase_if(waiting, [&](const scene::LevelEntity* entity) {
            if (taken.contains(entity->parent) && !placed.contains(entity->parent)) {
                return false;
            }
            copies.push_back(*entity);
            placed.insert(entity->id);
            return true;
        });
        if (waiting.size() == before) {
            break; // A loop, which a valid level does not have.
        }
    }
    return copies;
}

std::vector<std::uint64_t> CopiedRoots(std::span<const scene::LevelEntity> copies) {
    std::set<std::uint64_t> ids;
    for (const scene::LevelEntity& entity : copies) {
        ids.insert(entity.id);
    }
    std::vector<std::uint64_t> roots;
    for (const scene::LevelEntity& entity : copies) {
        if (!ids.contains(entity.parent)) {
            roots.push_back(entity.id);
        }
    }
    return roots;
}

std::vector<std::uint64_t> PasteEntities(scene::Level& level, std::vector<scene::LevelEntity> copies) {
    std::unordered_map<std::uint64_t, std::uint64_t> renamed;
    std::uint64_t next = level.NextId();
    for (const scene::LevelEntity& entity : copies) {
        renamed.emplace(entity.id, next++);
    }
    // An id of a copy becomes its new one; another stays if the level has it, else is dropped.
    const auto follow = [&](std::uint64_t id) -> std::uint64_t {
        if (id == 0) {
            return 0;
        }
        if (const auto found = renamed.find(id); found != renamed.end()) {
            return found->second;
        }
        return level.Find(id) != nullptr ? id : 0;
    };
    std::vector<std::uint64_t> roots;
    for (scene::LevelEntity& entity : copies) {
        const bool root = !renamed.contains(entity.parent);
        entity.parent = follow(entity.parent);
        if (entity.ribbon) {
            entity.ribbon->target = follow(entity.ribbon->target);
        }
        if (entity.lightning) {
            entity.lightning->light = follow(entity.lightning->light);
        }
        if (entity.particles && entity.particles->surface) {
            entity.particles->surface->entity = follow(entity.particles->surface->entity);
            if (entity.particles->surface->entity == 0) {
                entity.particles->surface.reset();
            }
        }
        entity.id = renamed.at(entity.id);
        if (root) {
            roots.push_back(entity.id);
        }
    }
    for (scene::LevelEntity& entity : copies) {
        level.entities.push_back(std::move(entity));
    }
    return roots;
}

bool IsAncestor(const scene::Level& level, std::uint64_t ancestor, std::uint64_t entity) {
    for (std::size_t steps = 0; entity != 0 && steps <= level.entities.size(); ++steps) {
        if (entity == ancestor) {
            return true;
        }
        const scene::LevelEntity* found = level.Find(entity);
        entity = found != nullptr ? found->parent : 0;
    }
    return false;
}

scene::Level PrefabOf(const scene::Level& level, std::span<const std::uint64_t> selection,
                      const std::function<math::Transform(const scene::LevelEntity&)>& place) {
    std::vector<scene::LevelEntity> copies = CopyEntities(level, selection);
    const std::vector<std::uint64_t> roots = CopiedRoots(copies);
    for (scene::LevelEntity& copy : copies) {
        if (std::ranges::find(roots, copy.id) != roots.end()) {
            copy.transform = place(copy);
            copy.parent = 0;
        }
    }
    scene::Level prefab;
    PasteEntities(prefab, std::move(copies));
    return prefab;
}

std::vector<std::uint64_t> Unpack(scene::Level& level, std::uint64_t placing, scene::Level prefab) {
    scene::LevelEntity* holder = level.Find(placing);
    if (holder == nullptr) {
        return {};
    }
    holder->prefab.clear();
    // Its roots are pasted at the root, then hung from the entity: a prefab's ids may be the level's too.
    std::vector<std::uint64_t> roots = PasteEntities(level, std::move(prefab.entities));
    for (const std::uint64_t root : roots) {
        level.Find(root)->parent = placing;
    }
    return roots;
}

} // namespace editor
