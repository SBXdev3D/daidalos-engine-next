// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Sequence.hpp>

#include <glm/common.hpp>

#include <simdjson.h>

#include <algorithm>
#include <format>

#include "JsonTree.hpp"

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";
using Element = simdjson::dom::element;

// What a cue's object holds, read with the cue's name in errors.
class Reader {
public:
    Reader(std::string_view sequence, std::string_view cue, simdjson::dom::object object)
        : m_sequence(sequence), m_cue(cue), m_object(object) {}

    [[nodiscard]] tl::unexpected<core::Error> Invalid(std::string_view key, std::string_view what) const {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("{}: {} \"{}\" {}", m_sequence, m_cue, key, what));
    }
    // Every key must be known.
    [[nodiscard]] core::Result<void> Only(std::initializer_list<std::string_view> keys) const {
        for (const auto field : m_object) {
            if (std::ranges::find(keys, field.key) == keys.end()) {
                return Invalid(field.key, "is not a known key");
            }
        }
        return {};
    }
    core::Result<void> Number(std::string_view key, float& out) const {
        Element value;
        if (m_object[key].get(value) != simdjson::SUCCESS) {
            return {};
        }
        double number = 0.0;
        if (value.get_double().get(number) != simdjson::SUCCESS) {
            return Invalid(key, "must be a number");
        }
        out = static_cast<float>(number);
        return {};
    }
    core::Result<void> Text(std::string_view key, std::string& out, bool required = false) const {
        Element value;
        if (m_object[key].get(value) != simdjson::SUCCESS) {
            return required ? core::Result<void>(Invalid(key, "is missing")) : core::Result<void>{};
        }
        std::string_view text;
        if (value.get_string().get(text) != simdjson::SUCCESS) {
            return Invalid(key, "must be a string");
        }
        out = std::string(text);
        return {};
    }
    core::Result<void> Flag(std::string_view key, bool& out) const {
        Element value;
        if (m_object[key].get(value) != simdjson::SUCCESS) {
            return {};
        }
        if (value.get_bool().get(out) != simdjson::SUCCESS) {
            return Invalid(key, "must be true or false");
        }
        return {};
    }
    core::Result<void> Vector(std::string_view key, glm::vec3& out, bool required = false) const {
        Element value;
        if (m_object[key].get(value) != simdjson::SUCCESS) {
            return required ? core::Result<void>(Invalid(key, "is missing")) : core::Result<void>{};
        }
        simdjson::dom::array array;
        if (value.get_array().get(array) != simdjson::SUCCESS || array.size() != 3) {
            return Invalid(key, "must be a list of 3 numbers");
        }
        int index = 0;
        for (Element item : array) {
            double number = 0.0;
            if (item.get_double().get(number) != simdjson::SUCCESS) {
                return Invalid(key, "must be a list of 3 numbers");
            }
            out[index++] = static_cast<float>(number);
        }
        return {};
    }
    [[nodiscard]] simdjson::dom::object Object() const { return m_object; }

private:
    std::string_view m_sequence;
    std::string_view m_cue;
    simdjson::dom::object m_object;
};

core::Result<Cue> ReadCue(std::string_view sequence, simdjson::dom::object cueObject, std::size_t index) {
    Cue cue;
    std::string_view kind;
    simdjson::dom::object body;
    bool hasEvent = false;
    for (const auto field : cueObject) {
        if (field.key == "at") {
            double at = 0.0;
            if (field.value.get_double().get(at) != simdjson::SUCCESS || at < 0.0) {
                return core::MakeError(
                    core::ErrorCode::ParseError, kModule,
                    std::format("{}: cue {} \"at\" must be a number of seconds, 0 or more", sequence, index));
            }
            cue.at = static_cast<float>(at);
        } else if (field.key == "event") {
            std::string_view name;
            if (field.value.get_string().get(name) != simdjson::SUCCESS) {
                return core::MakeError(core::ErrorCode::ParseError, kModule,
                                       std::format("{}: cue {} \"event\" must be a name", sequence, index));
            }
            cue.kind = Cue::Kind::Event;
            cue.name = std::string(name);
            hasEvent = true;
        } else if (!kind.empty() || hasEvent || field.value.get_object().get(body) != simdjson::SUCCESS) {
            return core::MakeError(
                core::ErrorCode::ParseError, kModule,
                std::format("{}: cue {} must be its time and one kind, as an object", sequence, index));
        } else {
            kind = field.key;
        }
    }
    if (hasEvent) {
        return cue;
    }
    if (kind.empty()) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("{}: cue {} has no kind", sequence, index));
    }
    const std::string label = std::format("cue {} ({})", index, kind);
    const Reader reader(sequence, label, body);
    if (kind == "power") {
        cue.kind = Cue::Kind::Power;
        DAI_TRY(reader.Only({"circuit", "on"}));
        DAI_TRY(reader.Text("circuit", cue.name, true));
        DAI_TRY(reader.Flag("on", cue.on));
    } else if (kind == "light" || kind == "visible") {
        cue.kind = kind == "light" ? Cue::Kind::Light : Cue::Kind::Visible;
        DAI_TRY(reader.Only({"entity", "on"}));
        DAI_TRY(reader.Text("entity", cue.name, true));
        DAI_TRY(reader.Flag("on", cue.on));
    } else if (kind == "sound") {
        cue.kind = Cue::Kind::Sound;
        DAI_TRY(reader.Only({"clip", "entity", "volume", "ducking", "caption"}));
        DAI_TRY(reader.Text("clip", cue.path, true));
        DAI_TRY(reader.Text("caption", cue.caption));
        DAI_TRY(reader.Text("entity", cue.name));
        DAI_TRY(reader.Number("volume", cue.volume));
        DAI_TRY(reader.Number("ducking", cue.ducking));
    } else if (kind == "subtitle") {
        cue.kind = Cue::Kind::Subtitle;
        DAI_TRY(reader.Only({"text", "speaker", "seconds"}));
        DAI_TRY(reader.Text("text", cue.caption, true));
        DAI_TRY(reader.Text("speaker", cue.name, true));
        cue.seconds = 3.0f;
        DAI_TRY(reader.Number("seconds", cue.seconds));
    } else if (kind == "particles") {
        cue.kind = Cue::Kind::Particles;
        DAI_TRY(reader.Only({"effect", "entity", "count"}));
        DAI_TRY(reader.Text("effect", cue.path, true));
        DAI_TRY(reader.Text("entity", cue.name, true));
        float count = 1.0f;
        DAI_TRY(reader.Number("count", count));
        cue.count = static_cast<std::uint32_t>(std::max(count, 1.0f));
    } else if (kind == "move") {
        cue.kind = Cue::Kind::Move;
        DAI_TRY(reader.Only({"entity", "to", "seconds"}));
        DAI_TRY(reader.Text("entity", cue.name, true));
        DAI_TRY(reader.Vector("to", cue.position, true));
        DAI_TRY(reader.Number("seconds", cue.seconds));
    } else if (kind == "shake") {
        cue.kind = Cue::Kind::Shake;
        DAI_TRY(reader.Only({"strength", "seconds"}));
        cue.strength = 0.5f;
        cue.seconds = 0.5f;
        DAI_TRY(reader.Number("strength", cue.strength));
        DAI_TRY(reader.Number("seconds", cue.seconds));
    } else if (kind == "mix") {
        cue.kind = Cue::Kind::Mix;
        DAI_TRY(reader.Only({"effects", "music", "ambience", "interface", "lowpass", "seconds"}));
        DAI_TRY(reader.Number("effects", cue.mix.buses[0]));
        DAI_TRY(reader.Number("music", cue.mix.buses[1]));
        DAI_TRY(reader.Number("ambience", cue.mix.buses[2]));
        DAI_TRY(reader.Number("interface", cue.mix.buses[3]));
        DAI_TRY(reader.Number("lowpass", cue.mix.lowpass));
        cue.seconds = 0.5f;
        DAI_TRY(reader.Number("seconds", cue.seconds));
    } else if (kind == "camera") {
        cue.kind = Cue::Kind::Camera;
        DAI_TRY(reader.Only({"keys"}));
        simdjson::dom::array keys;
        if (body["keys"].get_array().get(keys) != simdjson::SUCCESS || keys.size() == 0) {
            return reader.Invalid("keys", "must be a list of keys");
        }
        std::size_t keyIndex = 0;
        for (Element item : keys) {
            simdjson::dom::object keyObject;
            if (item.get_object().get(keyObject) != simdjson::SUCCESS) {
                return reader.Invalid("keys", "must hold objects");
            }
            const std::string keyLabel = std::format("{} key {}", label, keyIndex++);
            const Reader key(sequence, keyLabel, keyObject);
            DAI_TRY(key.Only({"at", "position", "target", "aperture", "focus"}));
            CameraKey camera;
            DAI_TRY(key.Number("at", camera.at));
            DAI_TRY(key.Vector("position", camera.position, true));
            DAI_TRY(key.Vector("target", camera.target, true));
            DAI_TRY(key.Number("aperture", camera.aperture));
            DAI_TRY(key.Number("focus", camera.focus));
            cue.keys.push_back(camera);
        }
        std::ranges::sort(cue.keys, {}, &CameraKey::at);
    } else {
        return reader.Invalid(kind, "is not a kind of cue");
    }
    return cue;
}

} // namespace

float Sequence::Length() const {
    float length = 0.0f;
    for (const Cue& cue : cues) {
        float end = cue.at + std::max(cue.seconds, 0.0f);
        if (cue.kind == Cue::Kind::Camera && !cue.keys.empty()) {
            end = cue.at + cue.keys.back().at;
        }
        length = std::max(length, end);
    }
    return length;
}

core::Result<Sequence> ParseSequence(std::string_view json, std::string_view name) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(json.data(), json.size()).get(root) != simdjson::SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("{}: not valid JSON", name));
    }
    simdjson::dom::object object;
    simdjson::dom::array cues;
    if (root.get_object().get(object) != simdjson::SUCCESS ||
        object["cues"].get_array().get(cues) != simdjson::SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("{}: needs a list of \"cues\"", name));
    }
    Sequence sequence;
    if (std::string_view comment; object["comment"].get_string().get(comment) == simdjson::SUCCESS) {
        sequence.comment = std::string(comment);
    }
    for (const auto field : object) {
        if (field.key != "cues" && field.key != "comment") {
            return core::MakeError(core::ErrorCode::ParseError, kModule,
                                   std::format("{}: \"{}\" is not a known key", name, field.key));
        }
    }
    std::size_t index = 0;
    for (Element item : cues) {
        simdjson::dom::object cue;
        if (item.get_object().get(cue) != simdjson::SUCCESS) {
            return core::MakeError(core::ErrorCode::ParseError, kModule,
                                   std::format("{}: cue {} must be an object", name, index));
        }
        DAI_TRY_ASSIGN(Cue read, ReadCue(name, cue, index++));
        sequence.cues.push_back(std::move(read));
    }
    std::ranges::stable_sort(sequence.cues, {}, &Cue::at);
    return sequence;
}

std::string_view CueKindName(Cue::Kind kind) {
    switch (kind) {
    case Cue::Kind::Power:
        return "power";
    case Cue::Kind::Light:
        return "light";
    case Cue::Kind::Sound:
        return "sound";
    case Cue::Kind::Particles:
        return "particles";
    case Cue::Kind::Move:
        return "move";
    case Cue::Kind::Visible:
        return "visible";
    case Cue::Kind::Shake:
        return "shake";
    case Cue::Kind::Camera:
        return "camera";
    case Cue::Kind::Mix:
        return "mix";
    case Cue::Kind::Event:
        return "event";
    case Cue::Kind::Subtitle:
        return "subtitle";
    }
    return "event";
}

std::string WriteSequence(const Sequence& sequence) {
    using detail::Json;
    using detail::JsonArray;
    using detail::JsonObject;
    const auto number = [](float value) { return Json{static_cast<double>(value)}; };
    const auto text = [](const std::string& value) { return Json{value}; };
    const auto vector = [&number](const glm::vec3& value) {
        return Json{JsonArray{number(value.x), number(value.y), number(value.z)}};
    };
    std::vector<const Cue*> ordered;
    for (const Cue& cue : sequence.cues) {
        ordered.push_back(&cue);
    }
    std::ranges::stable_sort(ordered, {}, &Cue::at);
    JsonArray cues;
    for (const Cue* cue : ordered) {
        JsonObject object;
        object.emplace("at", number(cue->at));
        if (cue->kind == Cue::Kind::Event) {
            object.emplace("event", text(cue->name));
            cues.push_back(Json{std::move(object)});
            continue;
        }
        JsonObject body;
        switch (cue->kind) {
        case Cue::Kind::Power:
            body.emplace("circuit", text(cue->name));
            body.emplace("on", Json{cue->on});
            break;
        case Cue::Kind::Light:
        case Cue::Kind::Visible:
            body.emplace("entity", text(cue->name));
            body.emplace("on", Json{cue->on});
            break;
        case Cue::Kind::Sound:
            body.emplace("clip", text(cue->path));
            if (!cue->name.empty()) {
                body.emplace("entity", text(cue->name));
            }
            if (!cue->caption.empty()) {
                body.emplace("caption", text(cue->caption));
            }
            body.emplace("volume", number(cue->volume));
            body.emplace("ducking", number(cue->ducking));
            break;
        case Cue::Kind::Subtitle:
            body.emplace("text", text(cue->caption));
            body.emplace("speaker", text(cue->name));
            body.emplace("seconds", number(cue->seconds));
            break;
        case Cue::Kind::Particles:
            body.emplace("effect", text(cue->path));
            body.emplace("entity", text(cue->name));
            body.emplace("count", Json{static_cast<double>(cue->count)});
            break;
        case Cue::Kind::Move:
            body.emplace("entity", text(cue->name));
            body.emplace("to", vector(cue->position));
            body.emplace("seconds", number(cue->seconds));
            break;
        case Cue::Kind::Shake:
            body.emplace("strength", number(cue->strength));
            body.emplace("seconds", number(cue->seconds));
            break;
        case Cue::Kind::Mix:
            body.emplace("effects", number(cue->mix.buses[0]));
            body.emplace("music", number(cue->mix.buses[1]));
            body.emplace("ambience", number(cue->mix.buses[2]));
            body.emplace("interface", number(cue->mix.buses[3]));
            body.emplace("lowpass", number(cue->mix.lowpass));
            body.emplace("seconds", number(cue->seconds));
            break;
        case Cue::Kind::Camera: {
            JsonArray keys;
            for (const CameraKey& key : cue->keys) {
                JsonObject written;
                written.emplace("at", number(key.at));
                written.emplace("position", vector(key.position));
                written.emplace("target", vector(key.target));
                written.emplace("aperture", number(key.aperture));
                written.emplace("focus", number(key.focus));
                keys.push_back(Json{std::move(written)});
            }
            body.emplace("keys", Json{std::move(keys)});
            break;
        }
        case Cue::Kind::Event:
            break;
        }
        object.emplace(std::string(CueKindName(cue->kind)), Json{std::move(body)});
        cues.push_back(Json{std::move(object)});
    }
    JsonObject root;
    if (!sequence.comment.empty()) {
        root.emplace("comment", Json{sequence.comment});
    }
    root.emplace("cues", Json{std::move(cues)});
    return detail::WriteJson(Json{std::move(root)});
}

CameraKey CameraAt(const std::vector<CameraKey>& keys, float time) {
    if (keys.empty()) {
        return {};
    }
    if (time <= keys.front().at) {
        return keys.front();
    }
    for (std::size_t index = 1; index < keys.size(); ++index) {
        if (time <= keys[index].at) {
            const CameraKey& a = keys[index - 1];
            const CameraKey& b = keys[index];
            const float span = std::max(b.at - a.at, 1e-4f);
            // Eased in and out, as a camera operator moves.
            float t = std::clamp((time - a.at) / span, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t);
            return {.at = time,
                    .position = glm::mix(a.position, b.position, t),
                    .target = glm::mix(a.target, b.target, t),
                    .aperture = glm::mix(a.aperture, b.aperture, t),
                    .focus = glm::mix(a.focus, b.focus, t)};
        }
    }
    return keys.back();
}

} // namespace daidalos::scene
