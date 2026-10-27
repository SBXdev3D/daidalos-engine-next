// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Assert.hpp>
#include <daidalos/ecs/Entity.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace daidalos::ecs {

/// A component is plain data stored by value. Pools move components when others are removed,
/// so they must be movable; references to components are invalidated by any addition or removal
/// of the same component type.
template <class T>
concept Component = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
                    std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;

namespace detail {

/// Each component type gets a small index, assigned on first use, to find its pool in a World.
std::size_t NextComponentTypeIndex() noexcept;

template <Component T>
std::size_t ComponentTypeIndex() noexcept {
    static const std::size_t index = NextComponentTypeIndex();
    return index;
}

/// Sparse set: a sparse array maps an entity index to a position in the dense arrays, which hold
/// the entities and their components contiguously for iteration. Removal swaps the last element
/// into the hole (ADR-0006).
class PoolBase {
public:
    PoolBase() = default;
    PoolBase(const PoolBase&) = delete;
    PoolBase& operator=(const PoolBase&) = delete;
    PoolBase(PoolBase&&) noexcept = default;
    PoolBase& operator=(PoolBase&&) noexcept = default;
    virtual ~PoolBase() = default;

    [[nodiscard]] bool Contains(std::uint32_t index) const {
        return index < m_sparse.size() && m_sparse[index] != kAbsent;
    }
    [[nodiscard]] std::size_t Size() const { return m_dense.size(); }
    [[nodiscard]] std::span<const Entity> Entities() const { return m_dense; }

    /// Removes the component of the entity with this index, which must be present.
    virtual void Remove(std::uint32_t index) = 0;

    // A pool is locked while it is iterated: adding or removing its components would move the
    // elements being visited.
    void Lock() { ++m_locks; }
    void Unlock() { --m_locks; }
    [[nodiscard]] bool IsLocked() const { return m_locks > 0; }

protected:
    static constexpr std::uint32_t kAbsent = std::numeric_limits<std::uint32_t>::max();

    std::vector<std::uint32_t> m_sparse;
    std::vector<Entity> m_dense;
    int m_locks = 0;
};

template <Component T>
class Pool final : public PoolBase {
public:
    template <class... Args>
    T& Emplace(Entity entity, Args&&... args) {
        DAI_ASSERT(!IsLocked(), "Components cannot be added to a pool that is being iterated");
        const std::uint32_t index = entity.Index();
        if (index >= m_sparse.size()) {
            m_sparse.resize(std::size_t{index} + 1, kAbsent);
        }
        if constexpr (std::is_constructible_v<T, Args...>) {
            m_components.emplace_back(std::forward<Args>(args)...);
        } else {
            m_components.push_back(T{std::forward<Args>(args)...}); // Aggregates.
        }
        m_dense.push_back(entity);
        m_sparse[index] = static_cast<std::uint32_t>(m_dense.size() - 1);
        return m_components.back();
    }

    void Remove(std::uint32_t index) override {
        DAI_ASSERT(!IsLocked(), "Components cannot be removed from a pool that is being iterated");
        DAI_ASSERT(Contains(index), "The entity has no component in this pool");
        const std::uint32_t position = m_sparse[index];
        const std::size_t last = m_dense.size() - 1;
        if (position != last) {
            m_components[position] = std::move(m_components[last]);
            m_dense[position] = m_dense[last];
            m_sparse[m_dense[position].Index()] = position;
        }
        m_components.pop_back();
        m_dense.pop_back();
        m_sparse[index] = kAbsent;
    }

    [[nodiscard]] T& Get(std::uint32_t index) {
        DAI_ASSERT(Contains(index), "The entity has no component in this pool");
        return m_components[m_sparse[index]];
    }
    [[nodiscard]] const T& Get(std::uint32_t index) const {
        DAI_ASSERT(Contains(index), "The entity has no component in this pool");
        return m_components[m_sparse[index]];
    }

private:
    std::vector<T> m_components;
};

} // namespace detail

} // namespace daidalos::ecs
