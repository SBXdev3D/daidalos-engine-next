// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <string_view>

namespace daidalos::ecs {

/// Handle to an entity of a World: a slot index plus the generation of that slot (ADR-0006).
/// Destroying an entity increments the generation of its slot, so handles to a destroyed entity
/// never refer to a new entity that reuses the slot. A default-constructed Entity is null.
class Entity {
public:
    static constexpr std::uint32_t kNullIndex = std::numeric_limits<std::uint32_t>::max();

    constexpr Entity() = default;
    constexpr Entity(std::uint32_t index, std::uint32_t generation) : m_index(index), m_generation(generation) {}

    [[nodiscard]] constexpr std::uint32_t Index() const { return m_index; }
    [[nodiscard]] constexpr std::uint32_t Generation() const { return m_generation; }
    [[nodiscard]] constexpr bool IsNull() const { return m_index == kNullIndex; }

    constexpr bool operator==(const Entity&) const = default;

private:
    std::uint32_t m_index = kNullIndex;
    std::uint32_t m_generation = 0;
};

inline constexpr Entity kNullEntity{};

} // namespace daidalos::ecs

template <>
struct std::hash<daidalos::ecs::Entity> {
    std::size_t operator()(const daidalos::ecs::Entity& entity) const noexcept {
        return std::hash<std::uint64_t>{}((std::uint64_t{entity.Generation()} << 32) | entity.Index());
    }
};

/// Formats as "index:generation", or "null".
template <>
struct std::formatter<daidalos::ecs::Entity> : std::formatter<std::string_view> {
    auto format(const daidalos::ecs::Entity& entity, std::format_context& context) const {
        if (entity.IsNull()) {
            return std::formatter<std::string_view>::format("null", context);
        }
        return std::format_to(context.out(), "{}:{}", entity.Index(), entity.Generation());
    }
};
