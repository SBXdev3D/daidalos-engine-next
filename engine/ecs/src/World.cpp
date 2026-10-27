// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/ecs/World.hpp>

#include <atomic>
#include <limits>

namespace daidalos::ecs {

namespace detail {

std::size_t NextComponentTypeIndex() noexcept {
    static std::atomic<std::size_t> next{0};
    return next.fetch_add(1);
}

} // namespace detail

World::World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;
World::~World() = default;

Entity World::Create() {
    std::uint32_t index = 0;
    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        DAI_ASSERT(m_generations.size() < Entity::kNullIndex, "Too many entities");
        index = static_cast<std::uint32_t>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(false);
    }
    m_alive[index] = true;
    ++m_aliveCount;
    return Entity(index, m_generations[index]);
}

bool World::Destroy(Entity entity) {
    if (!IsAlive(entity)) {
        return false;
    }
    const std::uint32_t index = entity.Index();
    for (const std::unique_ptr<detail::PoolBase>& pool : m_pools) {
        if (pool && pool->Contains(index)) {
            pool->Remove(index);
        }
    }

    m_alive[index] = false;
    --m_aliveCount;
    // A slot whose generation would wrap around is retired, so that no old handle can match it again.
    if (m_generations[index] < std::numeric_limits<std::uint32_t>::max()) {
        ++m_generations[index];
        m_freeIndices.push_back(index);
    }
    return true;
}

bool World::IsAlive(Entity entity) const {
    const std::uint32_t index = entity.Index();
    return index < m_generations.size() && m_alive[index] && m_generations[index] == entity.Generation();
}

} // namespace daidalos::ecs
