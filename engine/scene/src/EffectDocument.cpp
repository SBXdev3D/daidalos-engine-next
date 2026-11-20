// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/EffectDocument.hpp>

#include <algorithm>

#include "JsonTree.hpp"

namespace daidalos::scene {

using detail::Json;
using detail::JsonArray;
using detail::JsonObject;

struct EffectDocument::Impl {
    Json root{JsonObject{}};

    /// The value at a dotted key, or null.
    [[nodiscard]] const Json* Find(std::string_view key) const {
        const Json* at = &root;
        while (true) {
            const auto* object = std::get_if<JsonObject>(&at->value);
            if (object == nullptr) {
                return nullptr;
            }
            const std::size_t dot = key.find('.');
            const auto found = object->find(key.substr(0, dot));
            if (found == object->end()) {
                return nullptr;
            }
            at = &found->second;
            if (dot == std::string_view::npos) {
                return at;
            }
            key = key.substr(dot + 1);
        }
    }

    /// The value at a dotted key, made, with the objects on its way.
    Json& Make(std::string_view key) {
        Json* at = &root;
        while (true) {
            if (!std::holds_alternative<JsonObject>(at->value)) {
                at->value = JsonObject{};
            }
            auto& object = std::get<JsonObject>(at->value);
            const std::size_t dot = key.find('.');
            Json& next = object[std::string(key.substr(0, dot))];
            if (dot == std::string_view::npos) {
                return next;
            }
            at = &next;
            key = key.substr(dot + 1);
        }
    }
};

namespace {

std::optional<double> NumberOf(const Json* json) {
    if (json == nullptr) {
        return std::nullopt;
    }
    if (const double* number = std::get_if<double>(&json->value)) {
        return *number;
    }
    return std::nullopt;
}

Json Numbers(std::initializer_list<double> values) {
    JsonArray array;
    for (const double value : values) {
        array.push_back(Json{value});
    }
    return Json{std::move(array)};
}

} // namespace

EffectDocument::EffectDocument() : m_impl(std::make_unique<Impl>()) {}
EffectDocument::EffectDocument(const EffectDocument& other) : m_impl(std::make_unique<Impl>(*other.m_impl)) {}
EffectDocument& EffectDocument::operator=(const EffectDocument& other) {
    if (this != &other) {
        m_impl = std::make_unique<Impl>(*other.m_impl);
    }
    return *this;
}
EffectDocument::EffectDocument(EffectDocument&&) noexcept = default;
EffectDocument& EffectDocument::operator=(EffectDocument&&) noexcept = default;
EffectDocument::~EffectDocument() = default;

core::Result<EffectDocument> EffectDocument::Parse(std::string_view text) {
    core::Result<Json> json = detail::ParseJson(text, "effect");
    if (!json) {
        return tl::unexpected(json.error());
    }
    if (!std::holds_alternative<JsonObject>(json->value)) {
        return core::MakeError(core::ErrorCode::ParseError, "scene", "An effect file must be a JSON object");
    }
    EffectDocument document;
    document.m_impl->root = std::move(*json);
    return document;
}

std::string EffectDocument::Write() const {
    return detail::WriteJson(m_impl->root);
}

bool EffectDocument::Has(std::string_view key) const {
    return m_impl->Find(key) != nullptr;
}

void EffectDocument::Remove(std::string_view key) {
    const std::size_t dot = key.rfind('.');
    Json* holder = &m_impl->root;
    if (dot != std::string_view::npos) {
        holder = const_cast<Json*>(m_impl->Find(key.substr(0, dot)));
    }
    if (holder == nullptr) {
        return;
    }
    if (auto* object = std::get_if<JsonObject>(&holder->value)) {
        const auto found = object->find(key.substr(dot == std::string_view::npos ? 0 : dot + 1));
        if (found != object->end()) {
            object->erase(found);
        }
    }
}

std::optional<double> EffectDocument::Number(std::string_view key) const {
    return NumberOf(m_impl->Find(key));
}

void EffectDocument::SetNumber(std::string_view key, double value) {
    m_impl->Make(key) = Json{value};
}

std::optional<bool> EffectDocument::Flag(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    const bool* flag = json != nullptr ? std::get_if<bool>(&json->value) : nullptr;
    return flag != nullptr ? std::optional<bool>(*flag) : std::nullopt;
}

void EffectDocument::SetFlag(std::string_view key, bool value) {
    m_impl->Make(key) = Json{value};
}

std::optional<std::string> EffectDocument::Text(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    const std::string* text = json != nullptr ? std::get_if<std::string>(&json->value) : nullptr;
    return text != nullptr ? std::optional<std::string>(*text) : std::nullopt;
}

void EffectDocument::SetText(std::string_view key, std::string value) {
    m_impl->Make(key) = Json{std::move(value)};
}

std::optional<std::pair<double, double>> EffectDocument::Range(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    if (const std::optional<double> single = NumberOf(json)) {
        return std::pair{*single, *single};
    }
    const JsonArray* array = json != nullptr ? std::get_if<JsonArray>(&json->value) : nullptr;
    if (array == nullptr || array->size() != 2) {
        return std::nullopt;
    }
    const std::optional<double> low = NumberOf(&(*array)[0]);
    const std::optional<double> high = NumberOf(&(*array)[1]);
    return low && high ? std::optional(std::pair{*low, *high}) : std::nullopt;
}

void EffectDocument::SetRange(std::string_view key, double low, double high) {
    m_impl->Make(key) = low == high ? Json{low} : Numbers({low, high});
}

std::optional<glm::vec3> EffectDocument::Vector(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    const JsonArray* array = json != nullptr ? std::get_if<JsonArray>(&json->value) : nullptr;
    if (array == nullptr || array->size() != 3) {
        return std::nullopt;
    }
    glm::vec3 value{0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        const std::optional<double> number = NumberOf(&(*array)[static_cast<std::size_t>(axis)]);
        if (!number) {
            return std::nullopt;
        }
        value[axis] = static_cast<float>(*number);
    }
    return value;
}

void EffectDocument::SetVector(std::string_view key, const glm::vec3& value) {
    m_impl->Make(key) = Numbers({value.x, value.y, value.z});
}

std::optional<std::vector<std::pair<float, float>>> EffectDocument::Curve(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    if (const std::optional<double> single = NumberOf(json)) {
        return std::vector<std::pair<float, float>>{{0.0f, static_cast<float>(*single)}};
    }
    const JsonArray* array = json != nullptr ? std::get_if<JsonArray>(&json->value) : nullptr;
    if (array == nullptr) {
        return std::nullopt;
    }
    std::vector<std::pair<float, float>> keys;
    for (const Json& entry : *array) {
        const JsonArray* pair = std::get_if<JsonArray>(&entry.value);
        if (pair == nullptr || pair->size() != 2 || !NumberOf(&(*pair)[0]) || !NumberOf(&(*pair)[1])) {
            return std::nullopt;
        }
        keys.emplace_back(static_cast<float>(*NumberOf(&(*pair)[0])), static_cast<float>(*NumberOf(&(*pair)[1])));
    }
    return keys;
}

void EffectDocument::SetCurve(std::string_view key, const std::vector<std::pair<float, float>>& keys) {
    if (keys.size() == 1 && keys.front().first == 0.0f) {
        m_impl->Make(key) = Json{static_cast<double>(keys.front().second)};
        return;
    }
    JsonArray array;
    for (const auto& [time, value] : keys) {
        array.push_back(Numbers({time, value}));
    }
    m_impl->Make(key) = Json{std::move(array)};
}

std::optional<std::vector<std::pair<float, glm::vec4>>> EffectDocument::Colors(std::string_view key) const {
    const Json* json = m_impl->Find(key);
    const JsonArray* array = json != nullptr ? std::get_if<JsonArray>(&json->value) : nullptr;
    if (array == nullptr) {
        return std::nullopt;
    }
    std::vector<std::pair<float, glm::vec4>> keys;
    for (const Json& entry : *array) {
        const JsonArray* pair = std::get_if<JsonArray>(&entry.value);
        const JsonArray* color =
            pair != nullptr && pair->size() == 2 ? std::get_if<JsonArray>(&(*pair)[1].value) : nullptr;
        if (color == nullptr || !NumberOf(&(*pair)[0]) || color->size() < 3 || color->size() > 4) {
            return std::nullopt;
        }
        glm::vec4 value{1.0f};
        for (std::size_t channel = 0; channel < color->size(); ++channel) {
            const std::optional<double> number = NumberOf(&(*color)[channel]);
            if (!number) {
                return std::nullopt;
            }
            value[static_cast<int>(channel)] = static_cast<float>(*number);
        }
        keys.emplace_back(static_cast<float>(*NumberOf(&(*pair)[0])), value);
    }
    return keys;
}

void EffectDocument::SetColors(std::string_view key, const std::vector<std::pair<float, glm::vec4>>& keys) {
    JsonArray array;
    for (const auto& [time, color] : keys) {
        JsonArray pair;
        pair.push_back(Json{static_cast<double>(time)});
        pair.push_back(Numbers({color.r, color.g, color.b, color.a}));
        array.push_back(Json{std::move(pair)});
    }
    m_impl->Make(key) = Json{std::move(array)};
}

} // namespace daidalos::scene
