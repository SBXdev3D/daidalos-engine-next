// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Flipbook.hpp"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <vector>

namespace daidalos::texturetool {

namespace {

// Lengths are in units of the cube's side, times in seconds; a frame is 1/30 s, in two steps.
constexpr float kFrameSeconds = 1.0f / 30.0f;
constexpr int kStepsPerFrame = 2;
// The temperature below which nothing glows.
constexpr float kGlowing = 0.15f;
// The fraction of a frame, from each edge, over which it fades out.
constexpr float kEdgeFade = 0.12f;

std::uint32_t Mix(std::uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

// Gradient noise (Perlin 2002), about -1 to 1, from a seed.
class Noise {
public:
    explicit Noise(std::uint32_t seed) : m_seed(seed) {}

    float operator()(const glm::vec3& p) const {
        const glm::vec3 cell = glm::floor(p);
        const glm::vec3 f = p - cell;
        const glm::vec3 fade = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        std::array<float, 8> corners{};
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 offset(static_cast<float>(corner & 1), static_cast<float>((corner >> 1) & 1),
                                   static_cast<float>((corner >> 2) & 1));
            const glm::ivec3 at = glm::ivec3(cell + offset);
            corners[static_cast<std::size_t>(corner)] = glm::dot(Gradient(at), f - offset);
        }
        const auto x = [&](int a, int b) {
            return glm::mix(corners[static_cast<std::size_t>(a)], corners[static_cast<std::size_t>(b)], fade.x);
        };
        return glm::mix(glm::mix(x(0, 1), x(2, 3), fade.y), glm::mix(x(4, 5), x(6, 7), fade.y), fade.z);
    }

private:
    // One of the twelve directions to the middles of a cube's edges.
    glm::vec3 Gradient(const glm::ivec3& at) const {
        static constexpr std::array<glm::vec3, 12> kDirections{
            glm::vec3(1, 1, 0), glm::vec3(-1, 1, 0), glm::vec3(1, -1, 0), glm::vec3(-1, -1, 0),
            glm::vec3(1, 0, 1), glm::vec3(-1, 0, 1), glm::vec3(1, 0, -1), glm::vec3(-1, 0, -1),
            glm::vec3(0, 1, 1), glm::vec3(0, -1, 1), glm::vec3(0, 1, -1), glm::vec3(0, -1, -1)};
        const std::uint32_t hash =
            Mix(static_cast<std::uint32_t>(at.x) * 73856093U ^ static_cast<std::uint32_t>(at.y) * 19349663U ^
                static_cast<std::uint32_t>(at.z) * 83492791U ^ m_seed * 2654435761U);
        return kDirections[hash % kDirections.size()];
    }

    std::uint32_t m_seed;
};

// Values on a cube of n cells a side; cell (x, y, z) is centered on (x + 0.5, y + 0.5, z + 0.5) in cell units.
struct Grid {
    explicit Grid(int size) : n(size), values(static_cast<std::size_t>(size) * size * size, 0.0f) {}

    float& At(int x, int y, int z) { return values[(static_cast<std::size_t>(z) * n + y) * n + x]; }
    [[nodiscard]] float At(int x, int y, int z) const { return values[(static_cast<std::size_t>(z) * n + y) * n + x]; }
    [[nodiscard]] float Get(int x, int y, int z) const {
        return (x < 0 || y < 0 || z < 0 || x >= n || y >= n || z >= n) ? 0.0f : At(x, y, z);
    }
    // Blended between the cells around a point, in cell units; nothing outside the cube.
    [[nodiscard]] float Sample(const glm::vec3& p) const {
        const glm::vec3 q = p - 0.5f;
        const glm::ivec3 i = glm::ivec3(glm::floor(q));
        const glm::vec3 f = q - glm::vec3(i);
        const auto row = [&](int y, int z) { return glm::mix(Get(i.x, y, z), Get(i.x + 1, y, z), f.x); };
        const float near = glm::mix(row(i.y, i.z), row(i.y + 1, i.z), f.y);
        const float far = glm::mix(row(i.y, i.z + 1), row(i.y + 1, i.z + 1), f.y);
        return glm::mix(near, far, f.z);
    }

    int n;
    std::vector<float> values;
};

// How a kind of flipbook burns and moves.
struct Recipe {
    glm::vec3 source{0.5f, 0.12f, 0.5f};
    float sourceRadius = 0.1f;
    float heat = 0.0f;     // Temperature the source holds, about 1 for flames.
    float smoke = 0.0f;    // Density the source adds per second.
    float soot = 0.0f;     // Density that hot cells make per second, per unit of temperature.
    float buoyancy = 0.0f; // Rise per second, per unit of temperature.
    float turbulence = 0.0f;
    float eddies = 4.0f; // Swirls across the cube.
    float cooling = 1.0f;
    float fading = 0.5f;
    float extinction = 20.0f; // Of density, per side of the cube.
    float flame = 0.0f;       // Of temperature: flames have coverage.
    float glow = 0.0f;
    float albedo = 0.9f;
    bool loops = true;
    // A ball of heat and smoke at the start, which swells outwards, instead of a source that keeps burning.
    bool burst = false;
    float expansion = 0.0f;
};

Recipe RecipeOf(FlipbookKind kind) {
    switch (kind) {
    case FlipbookKind::Fire:
        return {.source = {0.5f, 0.08f, 0.5f},
                .sourceRadius = 0.16f,
                .heat = 1.2f,
                .soot = 0.03f,
                .buoyancy = 1.6f,
                .turbulence = 0.6f,
                .eddies = 5.0f,
                .cooling = 1.3f,
                .fading = 1.5f,
                .extinction = 18.0f,
                .flame = 8.0f,
                .glow = 1.0f,
                .albedo = 0.5f};
    case FlipbookKind::Smoke:
        return {.sourceRadius = 0.11f,
                .heat = 0.4f,
                .smoke = 2.5f,
                .buoyancy = 0.55f,
                .turbulence = 0.25f,
                .eddies = 4.0f,
                .cooling = 0.8f,
                .fading = 0.35f,
                .extinction = 18.0f,
                .albedo = 0.9f};
    case FlipbookKind::Explosion:
        return {.source = {0.5f, 0.4f, 0.5f},
                .sourceRadius = 0.14f,
                .heat = 1.6f,
                .smoke = 1.0f,
                .soot = 0.9f,
                .buoyancy = 0.35f,
                .turbulence = 0.3f,
                .eddies = 5.0f,
                .cooling = 1.4f,
                .fading = 0.25f,
                .extinction = 22.0f,
                .flame = 5.0f,
                .glow = 1.0f,
                .albedo = 0.6f,
                .loops = false,
                .burst = true,
                .expansion = 0.7f};
    }
    return {};
}

// The color of a black body, linear, its largest channel 1, for a temperature of the simulation: 0 is dull
// red, 1 orange and 1.6 yellow-white (Helland's fit of the CIE colors, in kelvin from 1000 to 3900).
glm::vec3 BlackBody(float temperature) {
    const float hundreds = std::clamp(1000.0f + 1800.0f * temperature, 1000.0f, 6600.0f) / 100.0f;
    const float green = std::clamp(99.47f * std::log(hundreds) - 161.12f, 0.0f, 255.0f) / 255.0f;
    const float blue =
        hundreds <= 19.0f ? 0.0f : std::clamp(138.52f * std::log(hundreds - 10.0f) - 305.04f, 0.0f, 255.0f) / 255.0f;
    // To linear light from the fit's sRGB.
    const auto linear = [](float c) { return std::pow(c, 2.2f); };
    return {1.0f, linear(green), linear(blue)};
}

// A frame as rendered, premultiplied by coverage: emission, the light of each side, and coverage.
struct Frame {
    std::vector<glm::vec3> emission;
    std::vector<glm::vec3> positive;
    std::vector<glm::vec3> negative;
    std::vector<float> coverage;
};

class Simulation {
public:
    Simulation(const FlipbookSettings& settings, const Recipe& recipe)
        : m_recipe(recipe), m_n(static_cast<int>(settings.grid)), m_density(m_n), m_temperature(m_n),
          m_potential{Noise(settings.seed * 3U + 1U), Noise(settings.seed * 3U + 2U), Noise(settings.seed * 3U + 3U)},
          m_flicker(settings.seed * 3U + 4U) {
        if (recipe.burst) {
            Source(1.0f, true);
        }
    }

    void Step(float dt) {
        const int n = m_n;
        const float cell = 1.0f / static_cast<float>(n);
        // The velocity of each cell: the curl of three noises, divided by their frequency so that its size does
        // not grow with the eddies, plus buoyancy and, for a burst, the swell.
        std::array<Grid, 3> potential{Grid(n), Grid(n), Grid(n)};
        const glm::vec3 drift(0.0f, -0.6f * m_time, 0.3f * m_time);
        for (int z = 0; z < n; ++z) {
            for (int y = 0; y < n; ++y) {
                for (int x = 0; x < n; ++x) {
                    const glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) * cell * m_recipe.eddies + drift;
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        potential[axis].At(x, y, z) = m_potential[axis](p) / m_recipe.eddies;
                    }
                }
            }
        }
        std::array<Grid, 3> velocity{Grid(n), Grid(n), Grid(n)};
        const float swell = m_recipe.expansion * std::exp(-3.0f * m_time);
        for (int z = 0; z < n; ++z) {
            for (int y = 0; y < n; ++y) {
                for (int x = 0; x < n; ++x) {
                    const auto d = [&](const Grid& grid, int dx, int dy, int dz) {
                        return (grid.Get(x + dx, y + dy, z + dz) - grid.Get(x - dx, y - dy, z - dz)) / (2.0f * cell);
                    };
                    glm::vec3 u(d(potential[2], 0, 1, 0) - d(potential[1], 0, 0, 1),
                                d(potential[0], 0, 0, 1) - d(potential[2], 1, 0, 0),
                                d(potential[1], 1, 0, 0) - d(potential[0], 0, 1, 0));
                    u *= m_recipe.turbulence;
                    u.y += m_recipe.buoyancy * m_temperature.At(x, y, z);
                    if (swell > 0.0f) {
                        const glm::vec3 away = (glm::vec3(x, y, z) + 0.5f) * cell - m_recipe.source;
                        const float length = glm::length(away);
                        if (length > 1e-4f) {
                            u += away / length * swell;
                        }
                    }
                    velocity[0].At(x, y, z) = u.x;
                    velocity[1].At(x, y, z) = u.y;
                    velocity[2].At(x, y, z) = u.z;
                }
            }
        }
        // Each cell takes what was where its velocity came from (semi-Lagrangian advection).
        Grid density(n);
        Grid temperature(n);
        for (int z = 0; z < n; ++z) {
            for (int y = 0; y < n; ++y) {
                for (int x = 0; x < n; ++x) {
                    const glm::vec3 u(velocity[0].At(x, y, z), velocity[1].At(x, y, z), velocity[2].At(x, y, z));
                    const glm::vec3 from = glm::vec3(x, y, z) + 0.5f - u * dt * static_cast<float>(n);
                    density.At(x, y, z) = m_density.Sample(from);
                    temperature.At(x, y, z) = m_temperature.Sample(from);
                }
            }
        }
        m_density = std::move(density);
        m_temperature = std::move(temperature);
        if (!m_recipe.burst) {
            Source(dt, false);
        }
        // Hot cells make soot; everything cools and thins.
        const float cooling = std::exp(-m_recipe.cooling * dt);
        const float fading = std::exp(-m_recipe.fading * dt);
        for (std::size_t i = 0; i < m_density.values.size(); ++i) {
            m_density.values[i] = (m_density.values[i] + m_recipe.soot * m_temperature.values[i] * dt) * fading;
            m_temperature.values[i] *= cooling;
        }
        m_time += dt;
    }

    // Renders the cube seen along -Z, its +X to the right and +Y up, into size by size pixels.
    [[nodiscard]] Frame Render(int size) const {
        const int n = m_n;
        const float cell = 1.0f / static_cast<float>(n);
        Grid extinction(n);
        for (std::size_t i = 0; i < extinction.values.size(); ++i) {
            // Flames cover only where they glow: cooler air is clear.
            extinction.values[i] = m_recipe.extinction * m_density.values[i] +
                                   m_recipe.flame * std::max(m_temperature.values[i] - kGlowing, 0.0f);
        }
        // How much of a light from each side reaches each cell: from +X, +Y, +Z, then -X, -Y, -Z.
        std::array<Grid, 6> lit{Grid(n), Grid(n), Grid(n), Grid(n), Grid(n), Grid(n)};
        for (int side = 0; side < 6; ++side) {
            const int axis = side % 3;
            const bool positive = side < 3;
            for (int a = 0; a < n; ++a) {
                for (int b = 0; b < n; ++b) {
                    float depth = 0.0f;
                    for (int step = 0; step < n; ++step) {
                        const int along = positive ? n - 1 - step : step;
                        glm::ivec3 at;
                        at[axis] = along;
                        at[(axis + 1) % 3] = a;
                        at[(axis + 2) % 3] = b;
                        const float here = extinction.At(at.x, at.y, at.z) * cell;
                        lit[static_cast<std::size_t>(side)].At(at.x, at.y, at.z) = std::exp(-(depth + 0.5f * here));
                        depth += here;
                    }
                }
            }
        }
        Frame frame;
        const auto pixels = static_cast<std::size_t>(size) * size;
        frame.emission.resize(pixels);
        frame.positive.resize(pixels);
        frame.negative.resize(pixels);
        frame.coverage.resize(pixels);
        for (int py = 0; py < size; ++py) {
            for (int px = 0; px < size; ++px) {
                const float x = (static_cast<float>(px) + 0.5f) / static_cast<float>(size) * static_cast<float>(n);
                const float y =
                    (1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(size)) * static_cast<float>(n);
                float transmittance = 1.0f;
                glm::vec3 emission(0.0f);
                std::array<float, 6> scattered{};
                // From the front, +Z, where the camera is, to the back.
                for (int step = 0; step < n && transmittance > 1e-3f; ++step) {
                    const glm::vec3 p(x, y, static_cast<float>(n - step) - 0.5f);
                    const float sigma = extinction.Sample(p);
                    const float temperature = m_temperature.Sample(p);
                    const float absorbed = 1.0f - std::exp(-sigma * cell);
                    if (m_recipe.glow > 0.0f && temperature > kGlowing) {
                        const float hot = temperature - kGlowing;
                        emission += transmittance * m_recipe.glow * BlackBody(temperature) * hot * hot * cell;
                    }
                    for (std::size_t side = 0; side < 6; ++side) {
                        scattered[side] += transmittance * absorbed * m_recipe.albedo * lit[side].Sample(p);
                    }
                    transmittance *= 1.0f - absorbed;
                }
                // Fades out towards the frame's edges, where the plume leaves the cube: a quad shows no edge.
                const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(size);
                const float edge = std::clamp(std::min({u, 1.0f - u, v, 1.0f - v}) / kEdgeFade, 0.0f, 1.0f);
                const float window = edge * edge * (3.0f - 2.0f * edge);
                const std::size_t index = static_cast<std::size_t>(py) * size + px;
                frame.emission[index] = emission * window;
                frame.positive[index] = glm::vec3(scattered[0], scattered[1], scattered[2]) * window;
                frame.negative[index] = glm::vec3(scattered[3], scattered[4], scattered[5]) * window;
                frame.coverage[index] = (1.0f - transmittance) * window;
            }
        }
        return frame;
    }

private:
    // Adds the source: heat that it holds, a little flickering, and smoke; all of it at once for a burst.
    void Source(float dt, bool all) {
        const int n = m_n;
        const float cell = 1.0f / static_cast<float>(n);
        for (int z = 0; z < n; ++z) {
            for (int y = 0; y < n; ++y) {
                for (int x = 0; x < n; ++x) {
                    const glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) * cell;
                    const float weight =
                        std::clamp(1.0f - glm::length(p - m_recipe.source) / m_recipe.sourceRadius, 0.0f, 1.0f);
                    if (weight <= 0.0f) {
                        continue;
                    }
                    const float flicker = 0.75f + 0.25f * m_flicker(p * 8.0f + glm::vec3(0.0f, 0.0f, 4.0f * m_time));
                    float& temperature = m_temperature.At(x, y, z);
                    temperature = std::max(temperature, m_recipe.heat * std::min(weight * 2.0f, 1.0f) * flicker);
                    m_density.At(x, y, z) += m_recipe.smoke * weight * flicker * (all ? 1.0f : dt);
                }
            }
        }
    }

    Recipe m_recipe;
    int m_n;
    Grid m_density;
    Grid m_temperature;
    std::array<Noise, 3> m_potential;
    Noise m_flicker;
    float m_time = 0.0f;
};

std::uint8_t Byte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

float ToSrgb(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

} // namespace

core::Result<Flipbook> MakeFlipbook(const FlipbookSettings& settings) {
    if (settings.frames == 0 || settings.size < 8 || settings.grid < 8 || settings.grid > 256) {
        return core::MakeError(core::ErrorCode::InvalidArgument, "texture-tool",
                               "A flipbook needs frames, 8 pixels across or more, and a grid of 8 to 256 cells");
    }
    const Recipe recipe = RecipeOf(settings.kind);
    Simulation simulation(settings, recipe);
    const auto frames = static_cast<int>(settings.frames);
    // A looped flipbook starts once the flames have settled, and simulates a quarter more frames, which are
    // blended into its first ones.
    const int warmup = recipe.loops ? 45 : 0;
    const int overlap = recipe.loops ? std::max(frames / 4, 1) : 0;
    const float dt = kFrameSeconds / static_cast<float>(kStepsPerFrame);
    for (int frame = 0; frame < warmup * kStepsPerFrame; ++frame) {
        simulation.Step(dt);
    }
    std::vector<Frame> rendered;
    const auto size = static_cast<int>(settings.size);
    for (int frame = 0; frame < frames + overlap; ++frame) {
        if (frame > 0 || recipe.loops) {
            for (int step = 0; step < kStepsPerFrame; ++step) {
                simulation.Step(dt);
            }
        }
        rendered.push_back(simulation.Render(size));
    }
    for (int frame = 0; frame < overlap; ++frame) {
        // From the frame after the last, at the start, to the frame itself, at the end of the overlap.
        const float t = static_cast<float>(frame) / static_cast<float>(overlap);
        Frame& into = rendered[static_cast<std::size_t>(frame)];
        const Frame& after = rendered[static_cast<std::size_t>(frames + frame)];
        for (std::size_t i = 0; i < into.coverage.size(); ++i) {
            into.emission[i] = glm::mix(after.emission[i], into.emission[i], t);
            into.positive[i] = glm::mix(after.positive[i], into.positive[i], t);
            into.negative[i] = glm::mix(after.negative[i], into.negative[i], t);
            into.coverage[i] = glm::mix(after.coverage[i], into.coverage[i], t);
        }
    }
    rendered.resize(static_cast<std::size_t>(frames));

    // Emission is scaled so that the brightest of all frames is 1: the effect's curves set how bright it is.
    float brightest = 0.0f;
    for (const Frame& frame : rendered) {
        for (std::size_t i = 0; i < frame.coverage.size(); ++i) {
            if (frame.coverage[i] > 0.02f) {
                const glm::vec3 color = frame.emission[i] / frame.coverage[i];
                brightest = std::max(brightest, std::max({color.r, color.g, color.b}));
            }
        }
    }
    const float scale = brightest > 0.0f ? 1.0f / brightest : 0.0f;

    Flipbook flipbook;
    flipbook.columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(frames))));
    flipbook.rows = (settings.frames + flipbook.columns - 1) / flipbook.columns;
    const auto atlas = [&](resources::PixelFormat format) {
        resources::ImageData image;
        image.width = flipbook.columns * settings.size;
        image.height = flipbook.rows * settings.size;
        image.format = format;
        image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4, std::byte{0});
        return image;
    };
    flipbook.color = atlas(resources::PixelFormat::Rgba8Srgb);
    flipbook.lightPositive = atlas(resources::PixelFormat::Rgba8Unorm);
    flipbook.lightNegative = atlas(resources::PixelFormat::Rgba8Unorm);
    for (std::size_t index = 0; index < rendered.size(); ++index) {
        const Frame& frame = rendered[index];
        const std::size_t left = (index % flipbook.columns) * settings.size;
        const std::size_t top = (index / flipbook.columns) * settings.size;
        for (std::size_t y = 0; y < settings.size; ++y) {
            for (std::size_t x = 0; x < settings.size; ++x) {
                const std::size_t source = y * settings.size + x;
                const std::size_t target = ((top + y) * flipbook.color.width + left + x) * 4;
                const float coverage = frame.coverage[source];
                // Unpremultiplied: what the particle's color and light are where it covers.
                const float inverse = coverage > 1e-3f ? 1.0f / coverage : 0.0f;
                const glm::vec3 color =
                    recipe.glow > 0.0f ? frame.emission[source] * inverse * scale : glm::vec3(recipe.albedo);
                const glm::vec3 positive = frame.positive[source] * inverse;
                const glm::vec3 negative = frame.negative[source] * inverse;
                const std::array<std::uint8_t, 4> colorBytes{Byte(ToSrgb(color.r)), Byte(ToSrgb(color.g)),
                                                             Byte(ToSrgb(color.b)), Byte(coverage)};
                const std::array<std::uint8_t, 4> positiveBytes{Byte(positive.r), Byte(positive.g), Byte(positive.b),
                                                                255};
                const std::array<std::uint8_t, 4> negativeBytes{Byte(negative.r), Byte(negative.g), Byte(negative.b),
                                                                255};
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    flipbook.color.pixels[target + channel] = std::byte{colorBytes[channel]};
                    flipbook.lightPositive.pixels[target + channel] = std::byte{positiveBytes[channel]};
                    flipbook.lightNegative.pixels[target + channel] = std::byte{negativeBytes[channel]};
                }
            }
        }
    }
    return flipbook;
}

} // namespace daidalos::texturetool
