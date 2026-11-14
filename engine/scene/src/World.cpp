// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/ecs/World.hpp>

#include <atomic>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace daidalos::ecs {

namespace detail {

std::size_t NextComponentTypeIndex() noexcept {
    static std::atomic<std::size_t> next{0};
    return next.fetch_add(1);
}

} // namespace detail

namespace {

constexpr std::string_view kModule = "ecs";

tl::unexpected<core::Error> HierarchyError(std::string message) {
    return core::MakeError(core::ErrorCode::InvalidArgument, kModule, std::move(message));
}

} // namespace

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

    // The whole subtree goes, so only the link between the entity and its parent needs repairing.
    std::vector<Entity> doomed{entity};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        for (Entity child = FirstChild(doomed[i]); !child.IsNull(); child = NextSibling(child)) {
            doomed.push_back(child);
        }
    }
    const Entity parent = Parent(entity);
    Unlink(entity);
    PruneNode(parent);

    for (const Entity dead : doomed) {
        ReleaseSlot(dead);
    }
    return true;
}

void World::ReleaseSlot(Entity entity) {
    const std::uint32_t index = entity.Index();
    for (const std::unique_ptr<detail::PoolBase>& pool : m_pools) {
        if (pool && pool->Contains(index)) {
            pool->Remove(index);
        }
    }
    if (m_hierarchy.Contains(index)) {
        m_hierarchy.Remove(index);
    }

    m_alive[index] = false;
    --m_aliveCount;
    // A slot whose generation would wrap around is retired, so that no old handle can match it again.
    if (m_generations[index] < std::numeric_limits<std::uint32_t>::max()) {
        ++m_generations[index];
        m_freeIndices.push_back(index);
    }
}

bool World::IsAlive(Entity entity) const {
    const std::uint32_t index = entity.Index();
    return index < m_generations.size() && m_alive[index] && m_generations[index] == entity.Generation();
}

core::Result<void> World::SetParent(Entity child, Entity parent) {
    if (!IsAlive(child)) {
        return HierarchyError(std::format("Cannot set the parent of {}: it is not alive", child));
    }
    if (!parent.IsNull() && !IsAlive(parent)) {
        return HierarchyError(std::format("Cannot attach {} to {}: the parent is not alive", child, parent));
    }
    if (parent == child) {
        return HierarchyError(std::format("Cannot attach {} to itself", child));
    }
    if (!parent.IsNull() && IsDescendantOf(parent, child)) {
        return HierarchyError(
            std::format("Cannot attach {} to its descendant {}: this would create a cycle", child, parent));
    }

    const Entity oldParent = Parent(child);
    if (oldParent == parent) {
        return {};
    }
    Unlink(child);
    if (!parent.IsNull()) {
        Link(child, parent);
    }
    PruneNode(oldParent);
    PruneNode(child);
    return {};
}

Entity World::Parent(Entity entity) const {
    const detail::HierarchyNode* node = FindNode(entity);
    return node != nullptr ? node->parent : kNullEntity;
}

Entity World::FirstChild(Entity entity) const {
    const detail::HierarchyNode* node = FindNode(entity);
    return node != nullptr ? node->firstChild : kNullEntity;
}

Entity World::NextSibling(Entity entity) const {
    const detail::HierarchyNode* node = FindNode(entity);
    return node != nullptr ? node->nextSibling : kNullEntity;
}

std::size_t World::ChildCount(Entity entity) const {
    const detail::HierarchyNode* node = FindNode(entity);
    return node != nullptr ? node->childCount : 0;
}

bool World::IsDescendantOf(Entity entity, Entity ancestor) const {
    if (!IsAlive(ancestor)) {
        return false;
    }
    for (Entity current = Parent(entity); !current.IsNull(); current = Parent(current)) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

const detail::HierarchyNode* World::FindNode(Entity entity) const {
    if (!IsAlive(entity) || !m_hierarchy.Contains(entity.Index())) {
        return nullptr;
    }
    return &m_hierarchy.Get(entity.Index());
}

detail::HierarchyNode& World::Node(Entity entity) {
    if (!m_hierarchy.Contains(entity.Index())) {
        return m_hierarchy.Emplace(entity);
    }
    return m_hierarchy.Get(entity.Index());
}

void World::Link(Entity child, Entity parent) {
    // Both nodes are created first: creating one may move the other in the pool.
    Node(child);
    Node(parent);
    detail::HierarchyNode& childNode = Node(child);
    detail::HierarchyNode& parentNode = Node(parent);

    childNode.parent = parent;
    childNode.previousSibling = parentNode.lastChild;
    childNode.nextSibling = kNullEntity;
    if (parentNode.lastChild.IsNull()) {
        parentNode.firstChild = child;
    } else {
        Node(parentNode.lastChild).nextSibling = child;
    }
    parentNode.lastChild = child;
    ++parentNode.childCount;
}

void World::Unlink(Entity child) {
    if (!m_hierarchy.Contains(child.Index())) {
        return;
    }
    detail::HierarchyNode& childNode = Node(child);
    if (childNode.parent.IsNull()) {
        return;
    }
    detail::HierarchyNode& parentNode = Node(childNode.parent);

    if (childNode.previousSibling.IsNull()) {
        parentNode.firstChild = childNode.nextSibling;
    } else {
        Node(childNode.previousSibling).nextSibling = childNode.nextSibling;
    }
    if (childNode.nextSibling.IsNull()) {
        parentNode.lastChild = childNode.previousSibling;
    } else {
        Node(childNode.nextSibling).previousSibling = childNode.previousSibling;
    }
    --parentNode.childCount;

    childNode.parent = kNullEntity;
    childNode.previousSibling = kNullEntity;
    childNode.nextSibling = kNullEntity;
}

void World::PruneNode(Entity entity) {
    if (entity.IsNull() || !m_hierarchy.Contains(entity.Index())) {
        return;
    }
    const detail::HierarchyNode& node = m_hierarchy.Get(entity.Index());
    if (node.parent.IsNull() && node.childCount == 0) {
        m_hierarchy.Remove(entity.Index());
    }
}

} // namespace daidalos::ecs
