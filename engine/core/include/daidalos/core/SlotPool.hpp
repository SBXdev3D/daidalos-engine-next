// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Handle.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace daidalos::core {

/// Stores objects in reusable slots and refers to them with generational handles. Lookups of null
/// and stale handles report the object as absent. Pointers to objects are invalidated by Insert.
template <class T, class Tag>
class SlotPool {
public:
    using HandleType = Handle<Tag>;

    HandleType Insert(T value) {
        std::uint32_t index = 0;
        if (m_freeIndices.empty()) {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.emplace_back();
        } else {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
        }
        Slot& slot = m_slots[index];
        slot.value.emplace(std::move(value));
        ++m_size;
        return HandleType(index, slot.generation);
    }

    /// Returns the object, or nothing if the handle is null or stale.
    std::optional<T> Remove(HandleType handle) {
        if (TryGet(handle) == nullptr) {
            return std::nullopt;
        }
        Slot& slot = m_slots[handle.Index()];
        std::optional<T> removed = std::move(slot.value);
        slot.value.reset();
        slot.generation = (slot.generation == UINT32_MAX) ? 1 : slot.generation + 1;
        m_freeIndices.push_back(handle.Index());
        --m_size;
        return removed;
    }

    [[nodiscard]] T* TryGet(HandleType handle) { return const_cast<T*>(std::as_const(*this).TryGet(handle)); }

    [[nodiscard]] const T* TryGet(HandleType handle) const {
        if (handle.IsNull() || handle.Index() >= m_slots.size()) {
            return nullptr;
        }
        const Slot& slot = m_slots[handle.Index()];
        return (slot.generation == handle.Generation() && slot.value.has_value()) ? &*slot.value : nullptr;
    }

    /// The handle must refer to a live object.
    [[nodiscard]] T& Get(HandleType handle) {
        T* value = TryGet(handle);
        DAI_ASSERT(value != nullptr, "Null or stale handle");
        return *value;
    }

    [[nodiscard]] const T& Get(HandleType handle) const {
        const T* value = TryGet(handle);
        DAI_ASSERT(value != nullptr, "Null or stale handle");
        return *value;
    }

    [[nodiscard]] std::size_t Size() const { return m_size; }

    /// Calls function(T&) for every live object. Objects must not be inserted or removed meanwhile.
    template <class Function>
    void ForEach(Function&& function) {
        for (Slot& slot : m_slots) {
            if (slot.value.has_value()) {
                function(*slot.value);
            }
        }
    }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeIndices;
    std::size_t m_size = 0;
};

} // namespace daidalos::core
