// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/ParticleEffectFile.hpp>

#include <glm/trigonometric.hpp>

#include <simdjson.h>

#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";

using Element = simdjson::dom::element;

tl::unexpected<core::Error> Invalid(std::string_view key, std::string_view what) {
    return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("Particle effect: \"{}\" {}", key, what));
}

core::Result<float> Number(Element value, std::string_view key) {
    double number = 0.0;
    if (value.get_double().get(number) != simdjson::SUCCESS) {
        return Invalid(key, "must be a number");
    }
    return static_cast<float>(number);
}

core::Result<std::uint32_t> Count(Element value, std::string_view key) {
    std::uint64_t number = 0;
    if (value.get_uint64().get(number) != simdjson::SUCCESS || number > 0xFFFFFFFFULL) {
        return Invalid(key, "must be a whole number, 0 or more");
    }
    return static_cast<std::uint32_t>(number);
}

core::Result<bool> Boolean(Element value, std::string_view key) {
    bool flag = false;
    if (value.get_bool().get(flag) != simdjson::SUCCESS) {
        return Invalid(key, "must be true or false");
    }
    return flag;
}

core::Result<std::string_view> Text(Element value, std::string_view key) {
    std::string_view text;
    if (value.get_string().get(text) != simdjson::SUCCESS) {
        return Invalid(key, "must be a string");
    }
    return text;
}

// A list of numbers, from minimum to maximum long.
core::Result<std::vector<float>> Numbers(Element value, std::string_view key, std::size_t minimum,
                                         std::size_t maximum) {
    simdjson::dom::array array;
    if (value.get_array().get(array) != simdjson::SUCCESS || array.size() < minimum || array.size() > maximum) {
        return Invalid(key, minimum == maximum ? std::format("must be a list of {} numbers", minimum)
                                               : std::format("must be a list of {} to {} numbers", minimum, maximum));
    }
    std::vector<float> numbers;
    for (Element item : array) {
        DAI_TRY_ASSIGN(float number, Number(item, key));
        numbers.push_back(number);
    }
    return numbers;
}

core::Result<glm::vec3> Vector(Element value, std::string_view key) {
    DAI_TRY_ASSIGN(std::vector<float> numbers, Numbers(value, key, 3, 3));
    return glm::vec3(numbers[0], numbers[1], numbers[2]);
}

// A number, or the two numbers a value is picked between.
core::Result<glm::vec2> Range(Element value, std::string_view key) {
    if (value.is_number()) {
        DAI_TRY_ASSIGN(float number, Number(value, key));
        return glm::vec2(number);
    }
    DAI_TRY_ASSIGN(std::vector<float> numbers, Numbers(value, key, 2, 2));
    if (numbers[0] > numbers[1]) {
        return Invalid(key, "must go from the smaller number to the larger");
    }
    return glm::vec2(numbers[0], numbers[1]);
}

// The [time, value] pairs of a list of keys, with the time from 0 to 1.
core::Result<std::vector<std::pair<float, Element>>> Keys(Element value, std::string_view key) {
    simdjson::dom::array array;
    if (value.get_array().get(array) != simdjson::SUCCESS || array.size() == 0) {
        return Invalid(key, "must be a list of [time, value] keys");
    }
    std::vector<std::pair<float, Element>> keys;
    for (Element item : array) {
        simdjson::dom::array pair;
        if (item.get_array().get(pair) != simdjson::SUCCESS || pair.size() != 2) {
            return Invalid(key, "must be a list of [time, value] keys");
        }
        DAI_TRY_ASSIGN(float time, Number(pair.at(0).value_unsafe(), key));
        if (time < 0.0f || time > 1.0f) {
            return Invalid(key, "has a key whose time is not from 0 to 1");
        }
        keys.emplace_back(time, pair.at(1).value_unsafe());
    }
    return keys;
}

core::Result<std::vector<renderer::ParticleCurveKey>> Curve(Element value, std::string_view key) {
    if (value.is_number()) {
        DAI_TRY_ASSIGN(float number, Number(value, key));
        return std::vector<renderer::ParticleCurveKey>{{0.0f, number}};
    }
    DAI_TRY_ASSIGN(auto keys, Keys(value, key));
    std::vector<renderer::ParticleCurveKey> curve;
    for (const auto& [time, element] : keys) {
        DAI_TRY_ASSIGN(float number, Number(element, key));
        curve.push_back({time, number});
    }
    return curve;
}

core::Result<std::vector<renderer::ParticleColorKey>> Gradient(Element value, std::string_view key) {
    DAI_TRY_ASSIGN(auto keys, Keys(value, key));
    std::vector<renderer::ParticleColorKey> gradient;
    for (const auto& [time, element] : keys) {
        DAI_TRY_ASSIGN(std::vector<float> color, Numbers(element, key, 3, 4));
        gradient.push_back({time, glm::vec4(color[0], color[1], color[2], color.size() == 4 ? color[3] : 1.0f)});
    }
    return gradient;
}

core::Result<renderer::ParticleOrientation> Orientation(Element value, std::string_view key) {
    DAI_TRY_ASSIGN(std::string_view name, Text(value, key));
    if (name == "camera") {
        return renderer::ParticleOrientation::Camera;
    }
    if (name == "velocity") {
        return renderer::ParticleOrientation::Velocity;
    }
    if (name == "axis") {
        return renderer::ParticleOrientation::Axis;
    }
    if (name == "plane") {
        return renderer::ParticleOrientation::Plane;
    }
    return Invalid(key, "must be \"camera\", \"velocity\", \"axis\" or \"plane\"");
}

core::Result<renderer::ParticleShape> Shape(Element value, std::string_view key) {
    DAI_TRY_ASSIGN(std::string_view name, Text(value, key));
    if (name == "box") {
        return renderer::ParticleShape::Box;
    }
    if (name == "cylinder") {
        return renderer::ParticleShape::Cylinder;
    }
    if (name == "cone") {
        return renderer::ParticleShape::Cone;
    }
    if (name == "line") {
        return renderer::ParticleShape::Line;
    }
    return Invalid(key, "must be \"box\", \"cylinder\", \"cone\" or \"line\"");
}

core::Result<ParticleBurst> Burst(Element value) {
    simdjson::dom::object object;
    if (value.get_object().get(object) != simdjson::SUCCESS) {
        return Invalid("emitter.bursts", "must be a list of objects");
    }
    ParticleBurst burst;
    for (const auto [name, field] : object) {
        if (name == "time") {
            DAI_TRY_ASSIGN(burst.time, Number(field, "emitter.bursts.time"));
        } else if (name == "count") {
            DAI_TRY_ASSIGN(burst.count, Count(field, "emitter.bursts.count"));
        } else if (name == "interval") {
            DAI_TRY_ASSIGN(burst.interval, Number(field, "emitter.bursts.interval"));
        } else if (name == "repeats") {
            DAI_TRY_ASSIGN(burst.repeats, Count(field, "emitter.bursts.repeats"));
        } else {
            return Invalid(std::format("emitter.bursts.{}", name), "is not a key of a burst");
        }
    }
    return burst;
}

core::Result<ParticleEmission> Emission(Element value) {
    simdjson::dom::object object;
    if (value.get_object().get(object) != simdjson::SUCCESS) {
        return Invalid("emitter", "must be an object");
    }
    ParticleEmission emission;
    for (const auto [name, field] : object) {
        if (name == "rate") {
            DAI_TRY_ASSIGN(emission.rate, Number(field, "emitter.rate"));
        } else if (name == "ratePerMeter") {
            DAI_TRY_ASSIGN(emission.ratePerMeter, Number(field, "emitter.ratePerMeter"));
        } else if (name == "bursts") {
            simdjson::dom::array bursts;
            if (field.get_array().get(bursts) != simdjson::SUCCESS) {
                return Invalid("emitter.bursts", "must be a list of objects");
            }
            for (Element burst : bursts) {
                DAI_TRY_ASSIGN(ParticleBurst read, Burst(burst));
                emission.bursts.push_back(read);
            }
        } else {
            return Invalid(std::format("emitter.{}", name), "is not a key of an emitter");
        }
    }
    return emission;
}

// Reads one key of the effect's object into the file.
core::Result<void> Read(ParticleEffectFile& file, std::string_view key, Element value) {
    renderer::ParticleEffectDesc& desc = file.desc;
    if (key == "comment") {
        DAI_TRY(Text(value, key));
    } else if (key == "texture") {
        DAI_TRY_ASSIGN(std::string_view texture, Text(value, key));
        file.texture = texture;
    } else if (key == "columns") {
        DAI_TRY_ASSIGN(desc.columns, Count(value, key));
    } else if (key == "rows") {
        DAI_TRY_ASSIGN(desc.rows, Count(value, key));
    } else if (key == "cell") {
        DAI_TRY_ASSIGN(desc.cell, Count(value, key));
    } else if (key == "frames") {
        DAI_TRY_ASSIGN(desc.frames, Count(value, key));
    } else if (key == "frameRate") {
        DAI_TRY_ASSIGN(desc.frameRate, Number(value, key));
    } else if (key == "loop") {
        DAI_TRY_ASSIGN(desc.loop, Boolean(value, key));
    } else if (key == "randomStartFrame") {
        DAI_TRY_ASSIGN(desc.randomStartFrame, Boolean(value, key));
    } else if (key == "color") {
        DAI_TRY_ASSIGN(desc.color, Gradient(value, key));
    } else if (key == "fadeIn") {
        DAI_TRY_ASSIGN(desc.fadeIn, Number(value, key));
    } else if (key == "sizeOverLife") {
        DAI_TRY_ASSIGN(desc.sizeOverLife, Curve(value, key));
    } else if (key == "emissionOverLife") {
        DAI_TRY_ASSIGN(desc.emissionOverLife, Curve(value, key));
    } else if (key == "speedOverLife") {
        DAI_TRY_ASSIGN(desc.speedOverLife, Curve(value, key));
    } else if (key == "additive") {
        DAI_TRY_ASSIGN(desc.additive, Boolean(value, key));
    } else if (key == "lit") {
        DAI_TRY_ASSIGN(desc.lit, Boolean(value, key));
    } else if (key == "collides") {
        DAI_TRY_ASSIGN(desc.collides, Boolean(value, key));
    } else if (key == "bounce") {
        DAI_TRY_ASSIGN(desc.bounce, Number(value, key));
    } else if (key == "lifetime") {
        DAI_TRY_ASSIGN(desc.lifetime, Range(value, key));
    } else if (key == "size") {
        DAI_TRY_ASSIGN(desc.size, Range(value, key));
    } else if (key == "orientation") {
        DAI_TRY_ASSIGN(desc.orientation, Orientation(value, key));
    } else if (key == "axis") {
        DAI_TRY_ASSIGN(desc.axis, Vector(value, key));
    } else if (key == "stretch") {
        DAI_TRY_ASSIGN(desc.stretch, Number(value, key));
    } else if (key == "shape") {
        DAI_TRY_ASSIGN(desc.shape, Shape(value, key));
    } else if (key == "spawnBox") {
        DAI_TRY_ASSIGN(desc.spawnBox, Vector(value, key));
    } else if (key == "spawnRadius") {
        DAI_TRY_ASSIGN(desc.spawnRadius, Number(value, key));
    } else if (key == "direction") {
        DAI_TRY_ASSIGN(desc.direction, Vector(value, key));
    } else if (key == "spread") {
        DAI_TRY_ASSIGN(float degrees, Number(value, key));
        desc.spread = glm::radians(degrees);
    } else if (key == "speed") {
        DAI_TRY_ASSIGN(desc.speed, Range(value, key));
    } else if (key == "inheritVelocity") {
        DAI_TRY_ASSIGN(desc.inheritVelocity, Number(value, key));
    } else if (key == "gravity") {
        DAI_TRY_ASSIGN(desc.gravity, Number(value, key));
    } else if (key == "drag") {
        DAI_TRY_ASSIGN(desc.drag, Number(value, key));
    } else if (key == "rotationSpeed") {
        DAI_TRY_ASSIGN(float degrees, Number(value, key));
        desc.rotationSpeed = glm::radians(degrees);
    } else if (key == "softness") {
        DAI_TRY_ASSIGN(desc.softness, Number(value, key));
    } else if (key == "emitter") {
        DAI_TRY_ASSIGN(file.emission, Emission(value));
    } else {
        return Invalid(key, "is not a key of a particle effect");
    }
    return {};
}

} // namespace

core::Result<ParticleEffectFile> ParseParticleEffect(std::string_view json) {
    simdjson::dom::parser parser;
    const simdjson::padded_string padded(json);
    Element root;
    if (const simdjson::error_code error = parser.parse(padded).get(root); error != simdjson::SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Particle effect: not JSON ({})", simdjson::error_message(error)));
    }
    simdjson::dom::object object;
    if (root.get_object().get(object) != simdjson::SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, kModule, "Particle effect: the file must be an object");
    }
    ParticleEffectFile file;
    for (const auto [key, value] : object) {
        DAI_TRY(Read(file, key, value));
    }
    const renderer::ParticleEffectDesc& desc = file.desc;
    if (file.texture.empty()) {
        return Invalid("texture", "is required");
    }
    const std::uint32_t rows = desc.rows == 0 ? desc.columns : desc.rows;
    if (desc.columns == 0) {
        return Invalid("columns", "must be 1 or more");
    }
    if (desc.frames == 0 || desc.cell + desc.frames > desc.columns * rows) {
        return Invalid("frames", std::format("go past the grid of {} by {} cells", desc.columns, rows));
    }
    if (desc.lifetime.x <= 0.0f) {
        return Invalid("lifetime", "must be more than 0");
    }
    return file;
}

} // namespace daidalos::scene
