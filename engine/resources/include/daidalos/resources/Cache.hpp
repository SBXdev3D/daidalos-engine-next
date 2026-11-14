// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace daidalos::resources {

/// Values of one type, by asset identity. It keeps its values for as long as it lives: nothing is
/// evicted and nothing is counted (ADR-0016). Pointers stay valid until the cache is destroyed or
/// cleared. Failed loads are not stored, so that they are tried again.
template <class T>
class Cache {
public:
    [[nodiscard]] T* Find(std::string_view key) {
        const auto found = m_values.find(key);
        return (found == m_values.end()) ? nullptr : found->second.get();
    }

    /// Stores a value under a key that is not in use yet, and returns it.
    T& Insert(std::string key, T value) {
        auto stored = std::make_unique<T>(std::move(value));
        T& reference = *stored;
        m_values.insert_or_assign(std::move(key), std::move(stored));
        return reference;
    }

    /// Calls the function with each value.
    template <class Function>
    void ForEach(Function&& function) {
        for (auto& [key, value] : m_values) {
            function(*value);
        }
    }

    /// Calls the function with each key and value, in key order.
    template <class Function>
    void ForEachEntry(Function&& function) const {
        for (const auto& [key, value] : m_values) {
            function(key, *value);
        }
    }

    void Clear() { m_values.clear(); }
    [[nodiscard]] std::size_t Size() const { return m_values.size(); }

private:
    std::map<std::string, std::unique_ptr<T>, std::less<>> m_values;
};

} // namespace daidalos::resources
