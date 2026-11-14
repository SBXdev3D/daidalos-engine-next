// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace daidalos::core {

/// Generational reference to an object in a SlotPool. Tag makes handles of different kinds distinct
/// types. A default-constructed handle is null; a handle becomes stale when its object is removed,
/// even if the slot is reused.
template <class Tag>
class Handle {
public:
    constexpr Handle() = default;
    constexpr Handle(std::uint32_t index, std::uint32_t generation) : m_index(index), m_generation(generation) {}

    [[nodiscard]] constexpr std::uint32_t Index() const { return m_index; }
    [[nodiscard]] constexpr std::uint32_t Generation() const { return m_generation; }
    [[nodiscard]] constexpr bool IsNull() const { return m_generation == 0; }
    constexpr explicit operator bool() const { return !IsNull(); }

    constexpr bool operator==(const Handle&) const = default;

private:
    std::uint32_t m_index = 0;
    std::uint32_t m_generation = 0; // 0 is reserved for null handles.
};

} // namespace daidalos::core

template <class Tag>
struct std::hash<daidalos::core::Handle<Tag>> {
    std::size_t operator()(const daidalos::core::Handle<Tag>& handle) const noexcept {
        const std::uint64_t packed = (std::uint64_t{handle.Generation()} << 32U) | handle.Index();
        return std::hash<std::uint64_t>{}(packed);
    }
};
