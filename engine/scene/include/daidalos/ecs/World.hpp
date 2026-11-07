// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/ComponentPool.hpp>
#include <daidalos/ecs/Entity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace daidalos::ecs {

namespace detail {

/// Links of an entity in the hierarchy: children form a doubly linked list in attachment order.
/// Only entities that have a parent or children have a node.
struct HierarchyNode {
    Entity parent;
    Entity firstChild;
    Entity lastChild;
    Entity previousSibling;
    Entity nextSibling;
    std::uint32_t childCount = 0;
};

} // namespace detail

/// Owns entities and their components (ADR-0006). Single-threaded.
///
/// Validity rules:
/// - Operations that read (IsAlive, Has, TryGet, Remove, Destroy) accept stale and null handles
///   and report them as absent.
/// - Add and Get on a dead entity, and Add of a component the entity already has, are programming
///   errors (DAI_ASSERT).
/// - While Each iterates some component types, components of those types cannot be added or
///   removed, and entities that have them cannot be destroyed (DAI_ASSERT).
///
/// Hierarchy: an entity has at most one parent and any number of children. Destroying an entity
/// destroys its descendants. The links are kept by the World rather than in a public component,
/// so they cannot be edited into an inconsistent state.
class World {
public:
    World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    ~World();

    [[nodiscard]] Entity Create();
    /// Destroys the entity, its descendants and all their components. Returns false if the handle
    /// is null or stale.
    bool Destroy(Entity entity);
    [[nodiscard]] bool IsAlive(Entity entity) const;
    [[nodiscard]] std::size_t AliveCount() const { return m_aliveCount; }
    /// Calls the function with every live entity, by index. It must not create or destroy entities.
    template <class Function>
        requires std::is_invocable_v<Function&, Entity>
    void EachEntity(Function&& function) const {
        for (std::uint32_t index = 0; index < m_generations.size(); ++index) {
            if (m_alive[index]) {
                function(Entity(index, m_generations[index]));
            }
        }
    }

    /// Attaches the child as the last child of the parent, or detaches it when the parent is null.
    /// Fails with InvalidArgument when either entity is not alive or when the parent is the child
    /// or one of its descendants (a cycle). On failure nothing changes.
    core::Result<void> SetParent(Entity child, Entity parent);

    /// Null when the entity has no parent or is not alive.
    [[nodiscard]] Entity Parent(Entity entity) const;
    /// Children are listed from FirstChild through NextSibling, in attachment order.
    [[nodiscard]] Entity FirstChild(Entity entity) const;
    [[nodiscard]] Entity NextSibling(Entity entity) const;
    [[nodiscard]] std::size_t ChildCount(Entity entity) const;
    /// True when the ancestor is the entity's parent, grandparent, and so on. An entity is not its
    /// own descendant.
    [[nodiscard]] bool IsDescendantOf(Entity entity, Entity ancestor) const;

    /// Constructs the component in place, with a constructor or aggregate initialization.
    template <Component T, class... Args>
    T& Add(Entity entity, Args&&... args) {
        DAI_ASSERT(IsAlive(entity), "Components can only be added to a live entity");
        detail::Pool<T>& pool = PoolFor<T>();
        DAI_ASSERT(!pool.Contains(entity.Index()), "The entity already has this component");
        return pool.Emplace(entity, std::forward<Args>(args)...);
    }

    /// Returns false if the entity is not alive or does not have the component.
    template <Component T>
    bool Remove(Entity entity) {
        detail::Pool<T>* pool = FindPool<T>();
        if (pool == nullptr || !IsAlive(entity) || !pool->Contains(entity.Index())) {
            return false;
        }
        pool->Remove(entity.Index());
        return true;
    }

    template <Component T>
    [[nodiscard]] bool Has(Entity entity) const {
        const detail::Pool<T>* pool = FindPool<T>();
        return pool != nullptr && IsAlive(entity) && pool->Contains(entity.Index());
    }

    /// Null if the entity is not alive or does not have the component.
    template <Component T>
    [[nodiscard]] T* TryGet(Entity entity) {
        detail::Pool<T>* pool = FindPool<T>();
        if (pool == nullptr || !IsAlive(entity) || !pool->Contains(entity.Index())) {
            return nullptr;
        }
        return &pool->Get(entity.Index());
    }

    template <Component T>
    [[nodiscard]] const T* TryGet(Entity entity) const {
        const detail::Pool<T>* pool = FindPool<T>();
        if (pool == nullptr || !IsAlive(entity) || !pool->Contains(entity.Index())) {
            return nullptr;
        }
        return &pool->Get(entity.Index());
    }

    /// The entity must be alive and have the component.
    template <Component T>
    [[nodiscard]] T& Get(Entity entity) {
        T* component = TryGet<T>(entity);
        DAI_ASSERT(component != nullptr, "The entity is not alive or does not have this component");
        return *component;
    }

    template <Component T>
    [[nodiscard]] const T& Get(Entity entity) const {
        const T* component = TryGet<T>(entity);
        DAI_ASSERT(component != nullptr, "The entity is not alive or does not have this component");
        return *component;
    }

    /// Number of live entities that have the component.
    template <Component T>
    [[nodiscard]] std::size_t Count() const {
        const detail::Pool<T>* pool = FindPool<T>();
        return pool != nullptr ? pool->Size() : 0;
    }

    /// Calls function(Entity, Ts&...) for every entity that has all the components. The order is
    /// unspecified. Iteration walks the smallest of the pools and looks the others up.
    template <Component... Ts, class Function>
        requires(sizeof...(Ts) > 0) && std::is_invocable_v<Function&, Entity, Ts&...>
    void Each(Function&& function) {
        const std::tuple<detail::Pool<Ts>*...> pools{FindPool<Ts>()...};
        const std::array<detail::PoolBase*, sizeof...(Ts)> bases{std::get<detail::Pool<Ts>*>(pools)...};
        detail::PoolBase* smallest = bases[0];
        for (detail::PoolBase* pool : bases) {
            if (pool == nullptr) {
                return; // No entity has this component yet.
            }
            if (pool->Size() < smallest->Size()) {
                smallest = pool;
            }
        }

        const IterationLock<sizeof...(Ts)> lock(bases);
        for (const Entity entity : smallest->Entities()) {
            const std::uint32_t index = entity.Index();
            if ((std::get<detail::Pool<Ts>*>(pools)->Contains(index) && ...)) {
                function(entity, std::get<detail::Pool<Ts>*>(pools)->Get(index)...);
            }
        }
    }

private:
    template <std::size_t Count>
    class IterationLock {
    public:
        explicit IterationLock(const std::array<detail::PoolBase*, Count>& pools) : m_pools(pools) {
            for (detail::PoolBase* pool : m_pools) {
                pool->Lock();
            }
        }
        IterationLock(const IterationLock&) = delete;
        IterationLock& operator=(const IterationLock&) = delete;
        ~IterationLock() {
            for (detail::PoolBase* pool : m_pools) {
                pool->Unlock();
            }
        }

    private:
        std::array<detail::PoolBase*, Count> m_pools;
    };

    [[nodiscard]] const detail::HierarchyNode* FindNode(Entity entity) const;
    detail::HierarchyNode& Node(Entity entity);
    void Link(Entity child, Entity parent);
    void Unlink(Entity child);
    void PruneNode(Entity entity);
    void ReleaseSlot(Entity entity);

    template <Component T>
    [[nodiscard]] detail::Pool<T>* FindPool() const {
        const std::size_t type = detail::ComponentTypeIndex<T>();
        return type < m_pools.size() ? static_cast<detail::Pool<T>*>(m_pools[type].get()) : nullptr;
    }

    template <Component T>
    detail::Pool<T>& PoolFor() {
        const std::size_t type = detail::ComponentTypeIndex<T>();
        if (type >= m_pools.size()) {
            m_pools.resize(type + 1);
        }
        if (!m_pools[type]) {
            m_pools[type] = std::make_unique<detail::Pool<T>>();
        }
        return static_cast<detail::Pool<T>&>(*m_pools[type]);
    }

    // Per slot: current generation, and whether an entity occupies it.
    std::vector<std::uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<std::uint32_t> m_freeIndices;
    std::size_t m_aliveCount = 0;
    // Indexed by component type. Null for types this world has never stored.
    std::vector<std::unique_ptr<detail::PoolBase>> m_pools;
    detail::Pool<detail::HierarchyNode> m_hierarchy;
};

} // namespace daidalos::ecs
