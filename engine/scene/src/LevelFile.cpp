// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Level.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <simdjson.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "JsonTree.hpp"

namespace daidalos::scene {

namespace {

using detail::Dump;
using detail::FromSimdjson;
using detail::Json;
using detail::JsonArray;
using detail::JsonObject;

constexpr std::string_view kModule = "scene";
constexpr std::string_view kFormat = "daidalos-level";

tl::unexpected<core::Error> Invalid(std::string_view where, std::string_view what) {
    return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("Level: {} {}", where, what));
}

// Reading: an object whose keys are checked off as they are read, so that the ones left are unknown.
class Reader {
public:
    Reader(const JsonObject& object, std::string where) : m_object(&object), m_where(std::move(where)) {}

    [[nodiscard]] const std::string& Where() const { return m_where; }

    [[nodiscard]] const Json* Take(std::string_view key) {
        const auto found = m_object->find(key);
        if (found == m_object->end()) {
            return nullptr;
        }
        m_read.emplace(key);
        return &found->second;
    }

    core::Result<void> Number(std::string_view key, float& out) {
        if (const Json* value = Take(key)) {
            const double* number = std::get_if<double>(&value->value);
            if (number == nullptr) {
                return Invalid(Field(key), "must be a number");
            }
            out = static_cast<float>(*number);
        }
        return {};
    }

    core::Result<void> Angle(std::string_view key, float& radians) {
        float degrees = glm::degrees(radians);
        if (core::Result<void> read = Number(key, degrees); !read) {
            return read;
        }
        radians = glm::radians(degrees);
        return {};
    }

    core::Result<void> Count(std::string_view key, std::uint32_t& out) {
        if (const Json* value = Take(key)) {
            const double* number = std::get_if<double>(&value->value);
            if (number == nullptr || *number < 0.0 || *number > 4294967295.0 || std::floor(*number) != *number) {
                return Invalid(Field(key), "must be a whole number, 0 or more");
            }
            out = static_cast<std::uint32_t>(*number);
        }
        return {};
    }

    core::Result<void> Id(std::string_view key, std::uint64_t& out) {
        if (const Json* value = Take(key)) {
            const double* number = std::get_if<double>(&value->value);
            if (number == nullptr || *number < 1.0 || *number > 9007199254740992.0 || std::floor(*number) != *number) {
                return Invalid(Field(key), "must be an entity id, a whole number from 1");
            }
            out = static_cast<std::uint64_t>(*number);
        }
        return {};
    }

    core::Result<void> Flag(std::string_view key, bool& out) {
        if (const Json* value = Take(key)) {
            const bool* flag = std::get_if<bool>(&value->value);
            if (flag == nullptr) {
                return Invalid(Field(key), "must be true or false");
            }
            out = *flag;
        }
        return {};
    }

    core::Result<void> Text(std::string_view key, std::string& out) {
        if (const Json* value = Take(key)) {
            const std::string* text = std::get_if<std::string>(&value->value);
            if (text == nullptr) {
                return Invalid(Field(key), "must be a string");
            }
            out = *text;
        }
        return {};
    }

    template <glm::length_t N>
    core::Result<void> Vector(std::string_view key, glm::vec<N, float>& out) {
        if (const Json* value = Take(key)) {
            const JsonArray* array = std::get_if<JsonArray>(&value->value);
            if (array == nullptr || array->size() != static_cast<std::size_t>(N)) {
                return Invalid(Field(key), std::format("must be an array of {} numbers", N));
            }
            for (glm::length_t index = 0; index < N; ++index) {
                const double* number = std::get_if<double>(&(*array)[static_cast<std::size_t>(index)].value);
                if (number == nullptr) {
                    return Invalid(Field(key), std::format("must be an array of {} numbers", N));
                }
                out[index] = static_cast<float>(*number);
            }
        }
        return {};
    }

    /// The object under a key, if there is one.
    core::Result<std::optional<Reader>> Object(std::string_view key) {
        const Json* value = Take(key);
        if (value == nullptr) {
            return std::optional<Reader>{};
        }
        const JsonObject* object = std::get_if<JsonObject>(&value->value);
        if (object == nullptr) {
            return Invalid(Field(key), "must be an object");
        }
        return std::optional<Reader>(Reader(*object, Field(key)));
    }

    /// An error for the first key that was not read.
    core::Result<void> Finish() const {
        for (const auto& [key, value] : *m_object) {
            if (!m_read.contains(key)) {
                return Invalid(Field(key), "is not a known key");
            }
        }
        return {};
    }

private:
    [[nodiscard]] std::string Field(std::string_view key) const { return std::format("{}.{}", m_where, key); }

    const JsonObject* m_object;
    std::string m_where;
    std::set<std::string, std::less<>> m_read;
};

#define DAI_LEVEL_TRY(expression)                                                                                      \
    do {                                                                                                               \
        if (auto daiLevelResult = (expression); !daiLevelResult) {                                                     \
            return tl::unexpected(daiLevelResult.error());                                                             \
        }                                                                                                              \
    } while (false)

core::Result<renderer::LightType> LightTypeOf(std::string_view name, const std::string& where) {
    if (name == "point") {
        return renderer::LightType::Point;
    }
    if (name == "spot") {
        return renderer::LightType::Spot;
    }
    if (name == "directional") {
        return renderer::LightType::Directional;
    }
    return Invalid(where + ".type", "must be \"point\", \"spot\" or \"directional\"");
}

std::string_view LightTypeName(renderer::LightType type) {
    switch (type) {
    case renderer::LightType::Spot:
        return "spot";
    case renderer::LightType::Directional:
        return "directional";
    case renderer::LightType::Point:
        break;
    }
    return "point";
}

core::Result<Light> ReadLight(Reader reader) {
    Light light;
    std::string type = "point";
    DAI_LEVEL_TRY(reader.Text("type", type));
    DAI_LEVEL_TRY(LightTypeOf(type, reader.Where()).map([&](renderer::LightType value) { light.type = value; }));
    DAI_LEVEL_TRY(reader.Vector("color", light.color));
    DAI_LEVEL_TRY(reader.Number("intensity", light.intensity));
    DAI_LEVEL_TRY(reader.Number("temperature", light.temperature));
    DAI_LEVEL_TRY(reader.Number("range", light.range));
    DAI_LEVEL_TRY(reader.Angle("inner_angle", light.innerAngle));
    DAI_LEVEL_TRY(reader.Angle("outer_angle", light.outerAngle));
    DAI_LEVEL_TRY(reader.Number("radius", light.radius));
    DAI_LEVEL_TRY(reader.Angle("angular_radius", light.angularRadius));
    DAI_LEVEL_TRY(reader.Flag("enabled", light.enabled));
    DAI_LEVEL_TRY(reader.Text("profile", light.profile));
    DAI_LEVEL_TRY(reader.Text("cookie", light.cookie));
    core::Result<std::optional<Reader>> animation = reader.Object("animation");
    DAI_LEVEL_TRY(animation);
    if (animation->has_value()) {
        Reader& fields = **animation;
        std::string pattern = "steady";
        DAI_LEVEL_TRY(fields.Text("pattern", pattern));
        if (!LightPatternOf(pattern, light.animation.pattern)) {
            return Invalid(fields.Where() + ".pattern",
                           std::format("'{}' is no pattern: steady, flicker, buzz, failing, strobe or pulse", pattern));
        }
        DAI_LEVEL_TRY(fields.Number("rate", light.animation.rate));
        DAI_LEVEL_TRY(fields.Number("depth", light.animation.depth));
        DAI_LEVEL_TRY(fields.Text("circuit", light.animation.circuit));
        DAI_LEVEL_TRY(fields.Finish());
    }
    DAI_LEVEL_TRY(reader.Finish());
    return light;
}

core::Result<Body> ReadBody(Reader reader) {
    Body body;
    DAI_LEVEL_TRY(reader.Vector("size", body.size));
    DAI_LEVEL_TRY(reader.Vector("center", body.center));
    DAI_LEVEL_TRY(reader.Number("mass", body.mass));
    DAI_LEVEL_TRY(reader.Finish());
    return body;
}

core::Result<Trigger> ReadTrigger(Reader reader) {
    Trigger trigger;
    DAI_LEVEL_TRY(reader.Text("sequence", trigger.sequence));
    DAI_LEVEL_TRY(reader.Flag("once", trigger.once));
    DAI_LEVEL_TRY(reader.Flag("enabled", trigger.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return trigger;
}

constexpr std::array<std::string_view, 4> kBlockShapes{"box", "stairs", "ramp", "cylinder"};

core::Result<LevelBlock> ReadBlock(Reader reader) {
    LevelBlock block;
    std::string shape = "box";
    DAI_LEVEL_TRY(reader.Text("shape", shape));
    const auto found = std::ranges::find(kBlockShapes, shape);
    if (found == kBlockShapes.end()) {
        return Invalid(reader.Where() + ".shape", "must be box, stairs, ramp or cylinder");
    }
    block.shape = static_cast<BlockShape>(found - kBlockShapes.begin());
    DAI_LEVEL_TRY(reader.Vector("size", block.size));
    float steps = static_cast<float>(block.steps);
    float sides = static_cast<float>(block.sides);
    DAI_LEVEL_TRY(reader.Number("steps", steps));
    DAI_LEVEL_TRY(reader.Number("sides", sides));
    block.steps = static_cast<std::uint32_t>(std::clamp(steps, 1.0f, 64.0f));
    block.sides = static_cast<std::uint32_t>(std::clamp(sides, 3.0f, 128.0f));
    DAI_LEVEL_TRY(reader.Vector("color", block.color));
    DAI_LEVEL_TRY(reader.Finish());
    return block;
}

core::Result<LevelSound> ReadSound(Reader reader) {
    LevelSound sound;
    DAI_LEVEL_TRY(reader.Text("clip", sound.clip));
    DAI_LEVEL_TRY(reader.Number("volume", sound.volume));
    DAI_LEVEL_TRY(reader.Number("min_distance", sound.minDistance));
    DAI_LEVEL_TRY(reader.Number("max_distance", sound.maxDistance));
    DAI_LEVEL_TRY(reader.Flag("follows_light", sound.followsLight));
    DAI_LEVEL_TRY(reader.Finish());
    return sound;
}

core::Result<LevelEditing> ReadEditing(Reader reader) {
    LevelEditing editing;
    DAI_LEVEL_TRY(reader.Text("folder", editing.folder));
    DAI_LEVEL_TRY(reader.Flag("hidden", editing.hidden));
    DAI_LEVEL_TRY(reader.Flag("locked", editing.locked));
    DAI_LEVEL_TRY(reader.Finish());
    return editing;
}

core::Result<VisibilityCell> ReadCell(Reader reader) {
    VisibilityCell cell;
    DAI_LEVEL_TRY(reader.Flag("enabled", cell.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return cell;
}

core::Result<WetVolume> ReadWet(Reader reader) {
    WetVolume wet;
    DAI_LEVEL_TRY(reader.Number("wetness", wet.wetness));
    DAI_LEVEL_TRY(reader.Number("puddles", wet.puddles));
    DAI_LEVEL_TRY(reader.Number("puddle_height", wet.puddleHeight));
    DAI_LEVEL_TRY(reader.Number("rain", wet.rain));
    DAI_LEVEL_TRY(reader.Number("streaks", wet.streaks));
    DAI_LEVEL_TRY(reader.Number("drying_rate", wet.dryingRate));
    DAI_LEVEL_TRY(reader.Flag("enabled", wet.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return wet;
}

core::Result<Decal> ReadDecal(Reader reader) {
    Decal decal;
    DAI_LEVEL_TRY(reader.Text("texture", decal.texture));
    DAI_LEVEL_TRY(reader.Text("normal", decal.normal));
    DAI_LEVEL_TRY(reader.Vector("color", decal.color));
    DAI_LEVEL_TRY(reader.Number("opacity", decal.opacity));
    DAI_LEVEL_TRY(reader.Number("roughness", decal.roughness));
    DAI_LEVEL_TRY(reader.Number("metallic", decal.metallic));
    DAI_LEVEL_TRY(reader.Number("angle_fade", decal.angleFade));
    DAI_LEVEL_TRY(reader.Flag("enabled", decal.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    if (decal.texture.empty()) {
        return Invalid(reader.Where(), "a decal needs a texture");
    }
    return decal;
}

core::Result<RayVisibility> ReadVisibility(Reader reader) {
    RayVisibility visibility;
    DAI_LEVEL_TRY(reader.Flag("camera", visibility.camera));
    DAI_LEVEL_TRY(reader.Flag("shadows", visibility.shadows));
    DAI_LEVEL_TRY(reader.Flag("reflections", visibility.reflections));
    DAI_LEVEL_TRY(reader.Flag("indirect", visibility.indirect));
    DAI_LEVEL_TRY(reader.Finish());
    return visibility;
}

core::Result<FogVolume> ReadFog(Reader reader) {
    FogVolume fog;
    DAI_LEVEL_TRY(reader.Number("density", fog.density));
    DAI_LEVEL_TRY(reader.Vector("albedo", fog.albedo));
    DAI_LEVEL_TRY(reader.Number("edge", fog.edge));
    DAI_LEVEL_TRY(reader.Number("falloff_height", fog.falloffHeight));
    DAI_LEVEL_TRY(reader.Number("noise", fog.noise));
    DAI_LEVEL_TRY(reader.Number("noise_frequency", fog.noiseFrequency));
    DAI_LEVEL_TRY(reader.Vector("noise_velocity", fog.noiseVelocity));
    DAI_LEVEL_TRY(reader.Vector("glow", fog.glow));
    DAI_LEVEL_TRY(reader.Flag("enabled", fog.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return fog;
}

core::Result<WindVolume> ReadWind(Reader reader) {
    WindVolume wind;
    DAI_LEVEL_TRY(reader.Vector("velocity", wind.velocity));
    DAI_LEVEL_TRY(reader.Number("gust", wind.gust));
    DAI_LEVEL_TRY(reader.Number("gust_frequency", wind.gustFrequency));
    DAI_LEVEL_TRY(reader.Number("edge", wind.edge));
    DAI_LEVEL_TRY(reader.Flag("enabled", wind.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return wind;
}

core::Result<LevelParticles> ReadParticles(Reader reader) {
    LevelParticles particles;
    DAI_LEVEL_TRY(reader.Text("effect", particles.effect));
    DAI_LEVEL_TRY(reader.Flag("enabled", particles.enabled));
    core::Result<std::optional<Reader>> burst = reader.Object("burst");
    DAI_LEVEL_TRY(burst);
    if (burst->has_value()) {
        ParticleBurst value{.count = 1, .interval = 1.0f};
        DAI_LEVEL_TRY((*burst)->Count("count", value.count));
        DAI_LEVEL_TRY((*burst)->Number("interval", value.interval));
        DAI_LEVEL_TRY((*burst)->Finish());
        particles.burst = value;
    }
    core::Result<std::optional<Reader>> surface = reader.Object("surface");
    DAI_LEVEL_TRY(surface);
    if (surface->has_value()) {
        LevelNodeRef ref;
        DAI_LEVEL_TRY((*surface)->Id("entity", ref.entity));
        DAI_LEVEL_TRY((*surface)->Text("node", ref.node));
        DAI_LEVEL_TRY((*surface)->Finish());
        if (ref.entity == 0 || ref.node.empty()) {
            return Invalid(reader.Where() + ".surface", "needs an entity and a node");
        }
        particles.surface = std::move(ref);
    }
    DAI_LEVEL_TRY(reader.Finish());
    if (particles.effect.empty()) {
        return Invalid(reader.Where(), "needs an effect");
    }
    return particles;
}

core::Result<LevelAttractor> ReadAttractor(Reader reader) {
    LevelAttractor attractor;
    DAI_LEVEL_TRY(reader.Text("effect", attractor.effect));
    DAI_LEVEL_TRY(reader.Number("strength", attractor.strength));
    DAI_LEVEL_TRY(reader.Number("vortex", attractor.vortex));
    DAI_LEVEL_TRY(reader.Number("radius", attractor.radius));
    DAI_LEVEL_TRY(reader.Flag("enabled", attractor.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    return attractor;
}

core::Result<LevelWeather> ReadWeather(Reader reader) {
    LevelWeather weather;
    DAI_LEVEL_TRY(reader.Text("effect", weather.effect));
    DAI_LEVEL_TRY(reader.Number("rate", weather.rate));
    DAI_LEVEL_TRY(reader.Number("reach", weather.reach));
    DAI_LEVEL_TRY(reader.Flag("enabled", weather.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    if (weather.effect.empty()) {
        return Invalid(reader.Where(), "needs an effect");
    }
    return weather;
}

core::Result<LevelRibbon> ReadRibbon(Reader reader) {
    LevelRibbon ribbon;
    DAI_LEVEL_TRY(reader.Text("effect", ribbon.effect));
    DAI_LEVEL_TRY(reader.Id("target", ribbon.target));
    DAI_LEVEL_TRY(reader.Flag("enabled", ribbon.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    if (ribbon.effect.empty()) {
        return Invalid(reader.Where(), "needs an effect");
    }
    return ribbon;
}

core::Result<LevelLightning> ReadLightning(Reader reader) {
    LevelLightning lightning;
    DAI_LEVEL_TRY(reader.Text("bolt", lightning.bolt));
    DAI_LEVEL_TRY(reader.Id("light", lightning.light));
    DAI_LEVEL_TRY(reader.Number("flash", lightning.flash));
    DAI_LEVEL_TRY(reader.Vector("interval", lightning.interval));
    DAI_LEVEL_TRY(reader.Number("duration", lightning.duration));
    DAI_LEVEL_TRY(reader.Flag("enabled", lightning.enabled));
    DAI_LEVEL_TRY(reader.Finish());
    if (lightning.bolt.empty()) {
        return Invalid(reader.Where(), "needs a bolt");
    }
    return lightning;
}

core::Result<Tags> ReadGame(Reader reader, const JsonObject& object) {
    Tags tags;
    for (const auto& [key, value] : object) {
        const std::string* text = std::get_if<std::string>(&value.value);
        if (text == nullptr) {
            return Invalid(std::format("{}.{}", reader.Where(), key), "must be a string");
        }
        if (key == "kind") {
            tags.kind = *text;
        } else {
            tags.values.emplace(key, *text);
        }
    }
    return tags;
}

core::Result<math::Transform> ReadTransform(Reader reader) {
    math::Transform transform;
    DAI_LEVEL_TRY(reader.Vector("translation", transform.translation));
    glm::vec4 rotation(0.0f, 0.0f, 0.0f, 1.0f);
    DAI_LEVEL_TRY(reader.Vector("rotation", rotation));
    const float length = glm::length(rotation);
    if (length < 1e-6f) {
        return Invalid(reader.Where() + ".rotation", "must not be zero");
    }
    rotation /= length;
    transform.rotation = glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
    DAI_LEVEL_TRY(reader.Vector("scale", transform.scale));
    DAI_LEVEL_TRY(reader.Finish());
    return transform;
}

core::Result<LevelEntity> ReadEntity(const JsonObject& object, std::size_t index) {
    Reader reader(object, std::format("entities[{}]", index));
    LevelEntity entity;
    DAI_LEVEL_TRY(reader.Id("id", entity.id));
    if (entity.id == 0) {
        return Invalid(reader.Where(), "needs an id");
    }
    DAI_LEVEL_TRY(reader.Text("name", entity.name));
    DAI_LEVEL_TRY(reader.Id("parent", entity.parent));
    DAI_LEVEL_TRY(reader.Text("model", entity.model));
    DAI_LEVEL_TRY(reader.Text("prefab", entity.prefab));

    // Each component: read it if it is there.
    const auto component = [&reader]<class T>(std::string_view key, std::optional<T>& out,
                                              core::Result<T> (*read)(Reader)) -> core::Result<void> {
        core::Result<std::optional<Reader>> object = reader.Object(key);
        if (!object) {
            return tl::unexpected(object.error());
        }
        if (object->has_value()) {
            core::Result<T> value = read(std::move(**object));
            if (!value) {
                return tl::unexpected(value.error());
            }
            out = std::move(*value);
        }
        return {};
    };
    core::Result<std::optional<Reader>> transform = reader.Object("transform");
    DAI_LEVEL_TRY(transform);
    if (transform->has_value()) {
        core::Result<math::Transform> value = ReadTransform(std::move(**transform));
        DAI_LEVEL_TRY(value);
        entity.transform = *value;
    }
    DAI_LEVEL_TRY(component("light", entity.light, &ReadLight));
    DAI_LEVEL_TRY(component("block", entity.block, &ReadBlock));
    DAI_LEVEL_TRY(component("sound", entity.sound, &ReadSound));
    DAI_LEVEL_TRY(component("visibility", entity.visibility, &ReadVisibility));
    DAI_LEVEL_TRY(component("decal", entity.decal, &ReadDecal));
    DAI_LEVEL_TRY(component("wet", entity.wet, &ReadWet));
    DAI_LEVEL_TRY(component("cell", entity.cell, &ReadCell));
    DAI_LEVEL_TRY(component("trigger", entity.trigger, &ReadTrigger));
    DAI_LEVEL_TRY(component("body", entity.body, &ReadBody));
    DAI_LEVEL_TRY(component("fog", entity.fog, &ReadFog));
    DAI_LEVEL_TRY(component("wind", entity.wind, &ReadWind));
    DAI_LEVEL_TRY(component("particles", entity.particles, &ReadParticles));
    DAI_LEVEL_TRY(component("attractor", entity.attractor, &ReadAttractor));
    DAI_LEVEL_TRY(component("weather", entity.weather, &ReadWeather));
    DAI_LEVEL_TRY(component("ribbon", entity.ribbon, &ReadRibbon));
    DAI_LEVEL_TRY(component("lightning", entity.lightning, &ReadLightning));
    std::optional<LevelEditing> editing;
    DAI_LEVEL_TRY(component("editor", editing, &ReadEditing));
    if (const Json* overrides = reader.Take("overrides")) {
        const JsonArray* array = std::get_if<JsonArray>(&overrides->value);
        if (array == nullptr) {
            return Invalid(reader.Where() + ".overrides", "must be an array");
        }
        for (std::size_t slot = 0; slot < array->size(); ++slot) {
            const JsonObject* fields = std::get_if<JsonObject>(&(*array)[slot].value);
            if (fields == nullptr) {
                return Invalid(std::format("{}.overrides[{}]", reader.Where(), slot), "must be an object");
            }
            Reader part(*fields, std::format("{}.overrides[{}]", reader.Where(), slot));
            PrefabOverride change;
            DAI_LEVEL_TRY(part.Id("part", change.part));
            DAI_LEVEL_TRY(part.Flag("removed", change.removed));
            core::Result<std::optional<Reader>> moved = part.Object("transform");
            DAI_LEVEL_TRY(moved);
            if (moved->has_value()) {
                core::Result<math::Transform> value = ReadTransform(std::move(**moved));
                DAI_LEVEL_TRY(value);
                change.transform = *value;
            }
            core::Result<std::optional<Reader>> lit = part.Object("light");
            DAI_LEVEL_TRY(lit);
            if (lit->has_value()) {
                core::Result<Light> value = ReadLight(std::move(**lit));
                DAI_LEVEL_TRY(value);
                change.light = std::move(*value);
            }
            if (part.Take("model") != nullptr) {
                const Json* model = &fields->at("model");
                const std::string* text = std::get_if<std::string>(&model->value);
                if (text == nullptr) {
                    return Invalid(part.Where() + ".model", "must be a string");
                }
                change.model = *text;
            }
            DAI_LEVEL_TRY(part.Finish());
            entity.overrides.push_back(std::move(change));
        }
    }
    entity.editing = editing.value_or(LevelEditing{});
    if (const Json* game = reader.Take("game")) {
        const JsonObject* tags = std::get_if<JsonObject>(&game->value);
        if (tags == nullptr) {
            return Invalid(reader.Where() + ".game", "must be an object of strings");
        }
        core::Result<Tags> value = ReadGame(Reader(*tags, reader.Where() + ".game"), *tags);
        DAI_LEVEL_TRY(value);
        entity.game = std::move(*value);
    }
    DAI_LEVEL_TRY(reader.Finish());
    return entity;
}

core::Result<LevelEnvironment> ReadEnvironment(Reader reader) {
    LevelEnvironment environment;
    core::Result<std::optional<Reader>> sky = reader.Object("sky");
    DAI_LEVEL_TRY(sky);
    if (sky->has_value()) {
        DAI_LEVEL_TRY((*sky)->Text("faces", environment.skyFaces));
        DAI_LEVEL_TRY((*sky)->Number("intensity", environment.skyIntensity));
        DAI_LEVEL_TRY((*sky)->Number("yaw", environment.skyYaw));
        DAI_LEVEL_TRY((*sky)->Finish());
    }
    core::Result<std::optional<Reader>> fog = reader.Object("fog");
    DAI_LEVEL_TRY(fog);
    if (fog->has_value()) {
        DAI_LEVEL_TRY((*fog)->Number("density", environment.fogDensity));
        DAI_LEVEL_TRY((*fog)->Vector("albedo", environment.fogAlbedo));
        DAI_LEVEL_TRY((*fog)->Number("anisotropy", environment.fogAnisotropy));
        DAI_LEVEL_TRY((*fog)->Finish());
    }
    core::Result<std::optional<Reader>> exposure = reader.Object("exposure");
    DAI_LEVEL_TRY(exposure);
    if (exposure->has_value()) {
        DAI_LEVEL_TRY((*exposure)->Number("ev100", environment.ev100));
        DAI_LEVEL_TRY((*exposure)->Flag("auto", environment.autoExposure));
        DAI_LEVEL_TRY((*exposure)->Number("min", environment.exposureMin));
        DAI_LEVEL_TRY((*exposure)->Number("max", environment.exposureMax));
        DAI_LEVEL_TRY((*exposure)->Finish());
    }
    core::Result<std::optional<Reader>> wind = reader.Object("wind");
    DAI_LEVEL_TRY(wind);
    if (wind->has_value()) {
        DAI_LEVEL_TRY((*wind)->Vector("velocity", environment.wind));
        DAI_LEVEL_TRY((*wind)->Number("gust", environment.windGust));
        DAI_LEVEL_TRY((*wind)->Number("gust_frequency", environment.windGustFrequency));
        DAI_LEVEL_TRY((*wind)->Finish());
    }
    DAI_LEVEL_TRY(reader.Finish());
    return environment;
}

/// Every id once, every parent present, and no loop through parents.
core::Result<void> CheckIds(const Level& level) {
    std::map<std::uint64_t, std::uint64_t> parents;
    for (const LevelEntity& entity : level.entities) {
        if (!parents.emplace(entity.id, entity.parent).second) {
            return Invalid(std::format("entity {}", entity.id), "appears twice");
        }
    }
    for (const auto& [id, parent] : parents) {
        std::uint64_t step = parent;
        for (std::size_t depth = 0; step != 0; ++depth) {
            const auto found = parents.find(step);
            if (found == parents.end()) {
                return Invalid(std::format("entity {}", id), std::format("has a parent {} that does not exist", step));
            }
            if (step == id || depth > parents.size()) {
                return Invalid(std::format("entity {}", id), "is its own ancestor");
            }
            step = found->second;
        }
    }
    return {};
}

// Writing.

Json Number(float value) {
    return Json{static_cast<double>(value)};
}

template <glm::length_t N>
Json Vector(const glm::vec<N, float>& value) {
    JsonArray array;
    for (glm::length_t index = 0; index < N; ++index) {
        array.push_back(Number(value[index]));
    }
    return Json{std::move(array)};
}

/// Adds the field if it differs from its default.
template <class T>
void Put(JsonObject& object, std::string_view key, const T& value, const T& fallback) {
    if (value == fallback) {
        return;
    }
    if constexpr (std::is_same_v<T, float>) {
        object.emplace(key, Number(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        object.emplace(key, Json{value});
    } else if constexpr (std::is_same_v<T, std::string>) {
        object.emplace(key, Json{value});
    } else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::uint32_t>) {
        object.emplace(key, Json{static_cast<double>(value)});
    } else {
        object.emplace(key, Vector(value));
    }
}

void PutAngle(JsonObject& object, std::string_view key, float radians, float fallback) {
    if (radians != fallback) {
        object.emplace(key, Number(glm::degrees(radians)));
    }
}

Json WriteLight(const Light& light) {
    const Light fallback;
    JsonObject object;
    object.emplace("type", Json{std::string(LightTypeName(light.type))});
    Put(object, "color", light.color, fallback.color);
    Put(object, "intensity", light.intensity, fallback.intensity);
    Put(object, "temperature", light.temperature, fallback.temperature);
    Put(object, "range", light.range, fallback.range);
    if (light.type == renderer::LightType::Spot) {
        PutAngle(object, "inner_angle", light.innerAngle, fallback.innerAngle);
        PutAngle(object, "outer_angle", light.outerAngle, fallback.outerAngle);
    }
    Put(object, "radius", light.radius, fallback.radius);
    PutAngle(object, "angular_radius", light.angularRadius, fallback.angularRadius);
    Put(object, "enabled", light.enabled, fallback.enabled);
    Put(object, "profile", light.profile, fallback.profile);
    Put(object, "cookie", light.cookie, fallback.cookie);
    if (!(light.animation == fallback.animation)) {
        const LightAnimation still;
        JsonObject animation;
        animation.emplace("pattern", Json{std::string(LightPatternName(light.animation.pattern))});
        Put(animation, "rate", light.animation.rate, still.rate);
        Put(animation, "depth", light.animation.depth, still.depth);
        Put(animation, "circuit", light.animation.circuit, still.circuit);
        object.emplace("animation", Json{std::move(animation)});
    }
    return Json{std::move(object)};
}

Json WriteFog(const FogVolume& fog) {
    const FogVolume fallback;
    JsonObject object;
    Put(object, "density", fog.density, fallback.density);
    Put(object, "albedo", fog.albedo, fallback.albedo);
    Put(object, "edge", fog.edge, fallback.edge);
    Put(object, "falloff_height", fog.falloffHeight, fallback.falloffHeight);
    Put(object, "noise", fog.noise, fallback.noise);
    Put(object, "noise_frequency", fog.noiseFrequency, fallback.noiseFrequency);
    Put(object, "noise_velocity", fog.noiseVelocity, fallback.noiseVelocity);
    Put(object, "glow", fog.glow, fallback.glow);
    Put(object, "enabled", fog.enabled, fallback.enabled);
    return Json{std::move(object)};
}

Json WriteWind(const WindVolume& wind) {
    const WindVolume fallback;
    JsonObject object;
    Put(object, "velocity", wind.velocity, fallback.velocity);
    Put(object, "gust", wind.gust, fallback.gust);
    Put(object, "gust_frequency", wind.gustFrequency, fallback.gustFrequency);
    Put(object, "edge", wind.edge, fallback.edge);
    Put(object, "enabled", wind.enabled, fallback.enabled);
    return Json{std::move(object)};
}

Json WriteParticles(const LevelParticles& particles) {
    JsonObject object;
    object.emplace("effect", Json{particles.effect});
    Put(object, "enabled", particles.enabled, true);
    if (particles.burst.has_value()) {
        JsonObject burst;
        burst.emplace("count", Json{static_cast<double>(particles.burst->count)});
        burst.emplace("interval", Number(particles.burst->interval));
        object.emplace("burst", Json{std::move(burst)});
    }
    if (particles.surface.has_value()) {
        JsonObject surface;
        surface.emplace("entity", Json{static_cast<double>(particles.surface->entity)});
        surface.emplace("node", Json{particles.surface->node});
        object.emplace("surface", Json{std::move(surface)});
    }
    return Json{std::move(object)};
}

Json WriteAttractor(const LevelAttractor& attractor) {
    const LevelAttractor fallback;
    JsonObject object;
    Put(object, "effect", attractor.effect, fallback.effect);
    Put(object, "strength", attractor.strength, fallback.strength);
    Put(object, "vortex", attractor.vortex, fallback.vortex);
    Put(object, "radius", attractor.radius, fallback.radius);
    Put(object, "enabled", attractor.enabled, fallback.enabled);
    return Json{std::move(object)};
}

Json WriteWeather(const LevelWeather& weather) {
    const LevelWeather fallback;
    JsonObject object;
    object.emplace("effect", Json{weather.effect});
    Put(object, "rate", weather.rate, fallback.rate);
    Put(object, "reach", weather.reach, fallback.reach);
    Put(object, "enabled", weather.enabled, fallback.enabled);
    return Json{std::move(object)};
}

Json WriteRibbon(const LevelRibbon& ribbon) {
    JsonObject object;
    object.emplace("effect", Json{ribbon.effect});
    Put(object, "target", ribbon.target, std::uint64_t{0});
    Put(object, "enabled", ribbon.enabled, true);
    return Json{std::move(object)};
}

Json WriteLightning(const LevelLightning& lightning) {
    const LevelLightning fallback;
    JsonObject object;
    object.emplace("bolt", Json{lightning.bolt});
    Put(object, "light", lightning.light, fallback.light);
    Put(object, "flash", lightning.flash, fallback.flash);
    Put(object, "interval", lightning.interval, fallback.interval);
    Put(object, "duration", lightning.duration, fallback.duration);
    Put(object, "enabled", lightning.enabled, fallback.enabled);
    return Json{std::move(object)};
}

Json WriteEntity(const LevelEntity& entity) {
    JsonObject object;
    object.emplace("id", Json{static_cast<double>(entity.id)});
    Put(object, "name", entity.name, std::string{});
    Put(object, "parent", entity.parent, std::uint64_t{0});
    Put(object, "model", entity.model, std::string{});
    Put(object, "prefab", entity.prefab, std::string{});
    const math::Transform identity;
    const auto transformOf = [&identity](const math::Transform& value) {
        JsonObject transform;
        Put(transform, "translation", value.translation, identity.translation);
        const glm::quat& rotation = value.rotation;
        if (rotation != identity.rotation) {
            transform.emplace("rotation", Vector(glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w)));
        }
        Put(transform, "scale", value.scale, identity.scale);
        return Json{std::move(transform)};
    };
    if (entity.transform != identity) {
        object.emplace("transform", transformOf(entity.transform));
    }
    if (!entity.overrides.empty()) {
        JsonArray overrides;
        for (const PrefabOverride& change : entity.overrides) {
            JsonObject part;
            part.emplace("part", Json{static_cast<double>(change.part)});
            Put(part, "removed", change.removed, false);
            if (change.transform) {
                part.emplace("transform", transformOf(*change.transform));
            }
            if (change.light) {
                part.emplace("light", WriteLight(*change.light));
            }
            if (change.model) {
                part.emplace("model", Json{*change.model});
            }
            overrides.push_back(Json{std::move(part)});
        }
        object.emplace("overrides", Json{std::move(overrides)});
    }
    if (entity.light) {
        object.emplace("light", WriteLight(*entity.light));
    }
    if (entity.sound) {
        const LevelSound fallback;
        JsonObject sound;
        sound.emplace("clip", Json{entity.sound->clip});
        Put(sound, "volume", entity.sound->volume, fallback.volume);
        Put(sound, "min_distance", entity.sound->minDistance, fallback.minDistance);
        Put(sound, "max_distance", entity.sound->maxDistance, fallback.maxDistance);
        Put(sound, "follows_light", entity.sound->followsLight, fallback.followsLight);
        object.emplace("sound", Json{std::move(sound)});
    }
    if (entity.block) {
        const LevelBlock fallback;
        JsonObject block;
        block.emplace("shape", Json{std::string(kBlockShapes[static_cast<std::size_t>(entity.block->shape)])});
        block.emplace("size", Vector(entity.block->size));
        Put(block, "steps", static_cast<float>(entity.block->steps), static_cast<float>(fallback.steps));
        Put(block, "sides", static_cast<float>(entity.block->sides), static_cast<float>(fallback.sides));
        Put(block, "color", entity.block->color, fallback.color);
        object.emplace("block", Json{std::move(block)});
    }
    if (entity.wet) {
        const WetVolume fallback;
        JsonObject wet;
        Put(wet, "wetness", entity.wet->wetness, fallback.wetness);
        Put(wet, "puddles", entity.wet->puddles, fallback.puddles);
        Put(wet, "puddle_height", entity.wet->puddleHeight, fallback.puddleHeight);
        Put(wet, "rain", entity.wet->rain, fallback.rain);
        Put(wet, "streaks", entity.wet->streaks, fallback.streaks);
        Put(wet, "drying_rate", entity.wet->dryingRate, fallback.dryingRate);
        Put(wet, "enabled", entity.wet->enabled, fallback.enabled);
        object.emplace("wet", Json{std::move(wet)});
    }
    if (entity.editing != LevelEditing{}) {
        JsonObject editor;
        Put(editor, "folder", entity.editing.folder, std::string{});
        Put(editor, "hidden", entity.editing.hidden, false);
        Put(editor, "locked", entity.editing.locked, false);
        object.emplace("editor", Json{std::move(editor)});
    }
    if (entity.cell) {
        JsonObject cell;
        Put(cell, "enabled", entity.cell->enabled, true);
        object.emplace("cell", Json{std::move(cell)});
    }
    if (entity.trigger) {
        const Trigger fallback;
        JsonObject trigger;
        trigger.emplace("sequence", Json{entity.trigger->sequence});
        Put(trigger, "once", entity.trigger->once, fallback.once);
        Put(trigger, "enabled", entity.trigger->enabled, fallback.enabled);
        object.emplace("trigger", Json{std::move(trigger)});
    }
    if (entity.body) {
        const Body fallback;
        JsonObject body;
        Put(body, "size", entity.body->size, fallback.size);
        Put(body, "center", entity.body->center, fallback.center);
        Put(body, "mass", entity.body->mass, fallback.mass);
        object.emplace("body", Json{std::move(body)});
    }
    if (entity.decal) {
        const Decal fallback;
        JsonObject decal;
        decal.emplace("texture", Json{entity.decal->texture});
        Put(decal, "normal", entity.decal->normal, fallback.normal);
        Put(decal, "color", entity.decal->color, fallback.color);
        Put(decal, "opacity", entity.decal->opacity, fallback.opacity);
        Put(decal, "roughness", entity.decal->roughness, fallback.roughness);
        Put(decal, "metallic", entity.decal->metallic, fallback.metallic);
        Put(decal, "angle_fade", entity.decal->angleFade, fallback.angleFade);
        Put(decal, "enabled", entity.decal->enabled, fallback.enabled);
        object.emplace("decal", Json{std::move(decal)});
    }
    if (entity.visibility) {
        const RayVisibility fallback;
        JsonObject visibility;
        Put(visibility, "camera", entity.visibility->camera, fallback.camera);
        Put(visibility, "shadows", entity.visibility->shadows, fallback.shadows);
        Put(visibility, "reflections", entity.visibility->reflections, fallback.reflections);
        Put(visibility, "indirect", entity.visibility->indirect, fallback.indirect);
        object.emplace("visibility", Json{std::move(visibility)});
    }
    if (entity.fog) {
        object.emplace("fog", WriteFog(*entity.fog));
    }
    if (entity.wind) {
        object.emplace("wind", WriteWind(*entity.wind));
    }
    if (entity.particles) {
        object.emplace("particles", WriteParticles(*entity.particles));
    }
    if (entity.attractor) {
        object.emplace("attractor", WriteAttractor(*entity.attractor));
    }
    if (entity.weather) {
        object.emplace("weather", WriteWeather(*entity.weather));
    }
    if (entity.ribbon) {
        object.emplace("ribbon", WriteRibbon(*entity.ribbon));
    }
    if (entity.lightning) {
        object.emplace("lightning", WriteLightning(*entity.lightning));
    }
    if (entity.game) {
        JsonObject game;
        game.emplace("kind", Json{entity.game->kind});
        for (const auto& [key, value] : entity.game->values) {
            game.emplace(key, Json{value});
        }
        object.emplace("game", Json{std::move(game)});
    }
    return Json{std::move(object)};
}

Json WriteEnvironment(const LevelEnvironment& environment) {
    const LevelEnvironment fallback;
    JsonObject object;
    if (!environment.skyFaces.empty()) {
        JsonObject sky;
        sky.emplace("faces", Json{environment.skyFaces});
        Put(sky, "intensity", environment.skyIntensity, fallback.skyIntensity);
        Put(sky, "yaw", environment.skyYaw, fallback.skyYaw);
        object.emplace("sky", Json{std::move(sky)});
    }
    JsonObject fog;
    Put(fog, "density", environment.fogDensity, fallback.fogDensity);
    Put(fog, "albedo", environment.fogAlbedo, fallback.fogAlbedo);
    Put(fog, "anisotropy", environment.fogAnisotropy, fallback.fogAnisotropy);
    if (!fog.empty()) {
        object.emplace("fog", Json{std::move(fog)});
    }
    JsonObject exposure;
    Put(exposure, "ev100", environment.ev100, fallback.ev100);
    Put(exposure, "auto", environment.autoExposure, fallback.autoExposure);
    Put(exposure, "min", environment.exposureMin, fallback.exposureMin);
    Put(exposure, "max", environment.exposureMax, fallback.exposureMax);
    if (!exposure.empty()) {
        object.emplace("exposure", Json{std::move(exposure)});
    }
    JsonObject wind;
    Put(wind, "velocity", environment.wind, fallback.wind);
    Put(wind, "gust", environment.windGust, fallback.windGust);
    Put(wind, "gust_frequency", environment.windGustFrequency, fallback.windGustFrequency);
    if (!wind.empty()) {
        object.emplace("wind", Json{std::move(wind)});
    }
    return Json{std::move(object)};
}

} // namespace

Tags ParseTags(std::string_view name) {
    Tags tags;
    std::size_t end = name.find(';');
    tags.kind = std::string(name.substr(0, end));
    while (end != std::string_view::npos) {
        const std::size_t start = end + 1;
        end = name.find(';', start);
        const std::string_view field = name.substr(start, end == std::string_view::npos ? end : end - start);
        const std::size_t equal = field.find('=');
        if (equal != std::string_view::npos) {
            tags.values.emplace(std::string(field.substr(0, equal)), std::string(field.substr(equal + 1)));
        }
    }
    return tags;
}

bool LevelParticles::operator==(const LevelParticles& other) const {
    const auto sameBurst = [](const std::optional<ParticleBurst>& a, const std::optional<ParticleBurst>& b) {
        if (a.has_value() != b.has_value()) {
            return false;
        }
        return !a.has_value() ||
               (a->time == b->time && a->count == b->count && a->interval == b->interval && a->repeats == b->repeats);
    };
    return effect == other.effect && sameBurst(burst, other.burst) && surface == other.surface &&
           enabled == other.enabled;
}

bool LevelEntity::operator==(const LevelEntity& other) const {
    return id == other.id && name == other.name && parent == other.parent && transform == other.transform &&
           model == other.model && prefab == other.prefab && light == other.light && fog == other.fog &&
           wind == other.wind && particles == other.particles && attractor == other.attractor &&
           weather == other.weather && ribbon == other.ribbon && lightning == other.lightning && game == other.game &&
           visibility == other.visibility && decal == other.decal && wet == other.wet && cell == other.cell &&
           trigger == other.trigger && body == other.body && editing == other.editing && overrides == other.overrides &&
           block == other.block && sound == other.sound;
}

const LevelEntity* Level::Find(std::uint64_t id) const {
    const auto found = std::ranges::find(entities, id, &LevelEntity::id);
    return found == entities.end() ? nullptr : &*found;
}

LevelEntity* Level::Find(std::uint64_t id) {
    const auto found = std::ranges::find(entities, id, &LevelEntity::id);
    return found == entities.end() ? nullptr : &*found;
}

std::uint64_t Level::NextId() const {
    std::uint64_t highest = 0;
    for (const LevelEntity& entity : entities) {
        highest = std::max(highest, entity.id);
    }
    return highest + 1;
}

core::Result<Level> ParseLevel(std::string_view json) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (const simdjson::error_code error = parser.parse(json.data(), json.size()).get(root); error) {
        return Invalid("file", std::format("is not valid JSON: {}", simdjson::error_message(error)));
    }
    core::Result<Json> tree = FromSimdjson(root);
    DAI_LEVEL_TRY(tree);
    const JsonObject* object = std::get_if<JsonObject>(&tree->value);
    if (object == nullptr) {
        return Invalid("file", "must be a JSON object");
    }
    Reader reader(*object, "level");
    std::string format;
    DAI_LEVEL_TRY(reader.Text("format", format));
    if (format != kFormat) {
        return Invalid("level.format", std::format("must be \"{}\"", kFormat));
    }
    std::uint32_t version = 0;
    DAI_LEVEL_TRY(reader.Count("version", version));
    if (version == 0 || version > kLevelVersion) {
        return Invalid("level.version",
                       std::format("is {}, and this engine reads versions 1 to {}", version, kLevelVersion));
    }
    Level level;
    core::Result<std::optional<Reader>> environment = reader.Object("environment");
    DAI_LEVEL_TRY(environment);
    if (environment->has_value()) {
        core::Result<LevelEnvironment> value = ReadEnvironment(std::move(**environment));
        DAI_LEVEL_TRY(value);
        level.environment = std::move(*value);
    }
    if (const Json* entities = reader.Take("entities")) {
        const JsonArray* array = std::get_if<JsonArray>(&entities->value);
        if (array == nullptr) {
            return Invalid("level.entities", "must be an array");
        }
        for (std::size_t index = 0; index < array->size(); ++index) {
            const JsonObject* entity = std::get_if<JsonObject>(&(*array)[index].value);
            if (entity == nullptr) {
                return Invalid(std::format("entities[{}]", index), "must be an object");
            }
            core::Result<LevelEntity> value = ReadEntity(*entity, index);
            DAI_LEVEL_TRY(value);
            level.entities.push_back(std::move(*value));
        }
    }
    DAI_LEVEL_TRY(reader.Finish());
    DAI_LEVEL_TRY(CheckIds(level));
    return level;
}

std::string WriteLevel(const Level& level) {
    JsonObject root;
    root.emplace("format", Json{std::string(kFormat)});
    root.emplace("version", Json{static_cast<double>(kLevelVersion)});
    root.emplace("environment", WriteEnvironment(level.environment));
    JsonArray entities;
    for (const LevelEntity& entity : level.entities) {
        entities.push_back(WriteEntity(entity));
    }
    root.emplace("entities", Json{std::move(entities)});
    std::string out;
    Dump(out, Json{std::move(root)}, 0);
    out += '\n';
    return out;
}

core::Result<Level> LoadLevelFile(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return core::MakeError(core::ErrorCode::NotFound, kModule,
                               std::format("Cannot open the level {}", file.generic_string()));
    }
    std::ostringstream text;
    text << stream.rdbuf();
    core::Result<Level> level = ParseLevel(text.str());
    if (!level) {
        core::Error error = level.error();
        error.message = std::format("{}: {}", file.generic_string(), error.message);
        return tl::unexpected(std::move(error));
    }
    return level;
}

const LevelEntity* FindBrokenTransform(const Level& level) {
    const auto finite = [](const math::Transform& transform) {
        const auto all = [](const auto& vector) {
            for (int axis = 0; axis < vector.length(); ++axis) {
                if (!std::isfinite(vector[axis])) {
                    return false;
                }
            }
            return true;
        };
        return all(transform.translation) && all(transform.rotation) && all(transform.scale);
    };
    const auto broken =
        std::ranges::find_if(level.entities, [&](const LevelEntity& entity) { return !finite(entity.transform); });
    return broken == level.entities.end() ? nullptr : &*broken;
}

core::Result<void> SaveLevelFile(const std::filesystem::path& file, const Level& level) {
    if (const LevelEntity* broken = FindBrokenTransform(level)) {
        return core::MakeError(
            core::ErrorCode::InvalidArgument, kModule,
            std::format("Not saved: the transform of \"{}\" (id {}) is not a number", broken->name, broken->id));
    }
    const std::string text = WriteLevel(level);
    std::filesystem::path temporary = file;
    temporary += ".saving";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) {
            return core::MakeError(core::ErrorCode::IoError, kModule,
                                   std::format("Cannot write the level {}", temporary.generic_string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot replace the level {}: {}", file.generic_string(), error.message()));
    }
    return {};
}

bool IsPrefabPath(std::string_view path) {
    constexpr std::string_view kSuffix = ".prefab.json";
    return path.size() > kSuffix.size() && path.ends_with(kSuffix);
}

Level LevelOfModel(std::string_view modelPath) {
    Level level;
    const std::filesystem::path path(modelPath);
    level.entities.push_back(
        LevelEntity{.id = 1, .name = path.stem().generic_string(), .model = path.filename().generic_string()});
    return level;
}

std::vector<std::string> LevelModels(const Level& level, std::string_view levelPath) {
    const std::string directory = std::filesystem::path(levelPath).parent_path().generic_string();
    std::vector<std::string> models;
    for (const LevelEntity& entity : level.entities) {
        if (entity.model.empty()) {
            continue;
        }
        std::string path = directory.empty() ? entity.model : directory + "/" + entity.model;
        if (std::ranges::find(models, path) == models.end()) {
            models.push_back(std::move(path));
        }
    }
    return models;
}

void Relocate(Level& level, std::string_view fromLevelPath, std::string_view toLevelPath) {
    const std::filesystem::path from = std::filesystem::path(fromLevelPath).parent_path();
    std::filesystem::path to = std::filesystem::path(toLevelPath).parent_path();
    if (to.empty()) {
        to = ".";
    }
    if (from.lexically_normal() == to.lexically_normal()) {
        return;
    }
    const auto move = [&](std::string& path) {
        if (!path.empty()) {
            path = (from / path).lexically_normal().lexically_relative(to).generic_string();
        }
    };
    move(level.environment.skyFaces);
    for (LevelEntity& entity : level.entities) {
        move(entity.model);
        move(entity.prefab);
        if (entity.trigger) {
            move(entity.trigger->sequence);
        }
        if (entity.light) {
            move(entity.light->profile);
            move(entity.light->cookie);
        }
        if (entity.decal) {
            move(entity.decal->texture);
            move(entity.decal->normal);
        }
        if (entity.sound) {
            move(entity.sound->clip);
        }
        if (entity.particles) {
            move(entity.particles->effect);
        }
        if (entity.attractor) {
            move(entity.attractor->effect);
        }
        if (entity.weather) {
            move(entity.weather->effect);
        }
        if (entity.ribbon) {
            move(entity.ribbon->effect);
        }
        if (entity.lightning) {
            move(entity.lightning->bolt);
        }
    }
}

} // namespace daidalos::scene
