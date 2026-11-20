// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daidalos::scene {

/// A particle effect or ribbon file as values to edit (ADR-0114): what the editor's effect panel reads and changes,
/// key by key, leaving the keys it does not know, and the comment, as they are. Keys are paths into objects:
/// "emitter.rate", "light.intensity". Written back, the keys are sorted, as the editor writes every file.
class EffectDocument {
public:
    [[nodiscard]] static core::Result<EffectDocument> Parse(std::string_view text);
    EffectDocument();
    EffectDocument(const EffectDocument& other);
    EffectDocument& operator=(const EffectDocument& other);
    EffectDocument(EffectDocument&&) noexcept;
    EffectDocument& operator=(EffectDocument&&) noexcept;
    ~EffectDocument();

    [[nodiscard]] std::string Write() const;

    [[nodiscard]] bool Has(std::string_view key) const;
    /// Takes a key out, so that the file's reader gives its default.
    void Remove(std::string_view key);

    [[nodiscard]] std::optional<double> Number(std::string_view key) const;
    void SetNumber(std::string_view key, double value);
    [[nodiscard]] std::optional<bool> Flag(std::string_view key) const;
    void SetFlag(std::string_view key, bool value);
    [[nodiscard]] std::optional<std::string> Text(std::string_view key) const;
    void SetText(std::string_view key, std::string value);
    /// Two numbers picked between, written as one number when they are the same.
    [[nodiscard]] std::optional<std::pair<double, double>> Range(std::string_view key) const;
    void SetRange(std::string_view key, double low, double high);
    [[nodiscard]] std::optional<glm::vec3> Vector(std::string_view key) const;
    void SetVector(std::string_view key, const glm::vec3& value);
    /// [time, value] keys, or a number that holds over the whole life (one key at time 0).
    [[nodiscard]] std::optional<std::vector<std::pair<float, float>>> Curve(std::string_view key) const;
    void SetCurve(std::string_view key, const std::vector<std::pair<float, float>>& keys);
    /// [time, [r, g, b, a]] keys.
    [[nodiscard]] std::optional<std::vector<std::pair<float, glm::vec4>>> Colors(std::string_view key) const;
    void SetColors(std::string_view key, const std::vector<std::pair<float, glm::vec4>>& keys);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::scene
