// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Thumbnail.hpp"

#include <daidalos/math/Transform.hpp>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace daidalos;

namespace editor {

namespace {

// Drawn at twice the size, then averaged down: edges come out smooth.
constexpr std::uint32_t kSupersample = 2;

float ToSrgb(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

struct Triangle {
    glm::vec3 corners[3];
    glm::vec3 color;
    glm::vec3 glow;
};

} // namespace

std::vector<std::uint8_t> RenderClay(const resources::ModelData& model, std::uint32_t size) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
    if (size == 0) {
        return pixels;
    }

    // The triangles in the model's space, nodes placed by their parents.
    std::vector<glm::mat4> worlds(model.nodes.size(), glm::mat4(1.0f));
    std::vector<Triangle> triangles;
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{-std::numeric_limits<float>::max()};
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const resources::ModelNode& node = model.nodes[index];
        const glm::mat4 local = math::ToMatrix(node.local);
        worlds[index] = node.parent ? worlds[*node.parent] * local : local;
        // A skinned mesh is in its bind pose, in the model's space.
        const glm::mat4 world = node.skin ? glm::mat4(1.0f) : worlds[index];
        for (const std::uint32_t primitiveIndex : node.primitives) {
            const resources::ModelPrimitive& primitive = model.primitives[primitiveIndex];
            glm::vec3 color{0.72f};
            glm::vec3 glow{0.0f};
            if (primitive.material && *primitive.material < model.materials.size()) {
                const resources::MaterialData& material = model.materials[*primitive.material];
                // White clay, tinted by the base color: a texture's colors are not drawn.
                color = glm::vec3(material.baseColorFactor) * 0.72f;
                glow = glm::min(material.emissiveFactor, glm::vec3(1.0f));
                if (material.transmission > 0.0f) {
                    color = glm::vec3(0.55f, 0.7f, 0.8f);
                }
            }
            const std::vector<resources::Vertex>& vertices = primitive.mesh.vertices;
            const std::vector<std::uint32_t>& indices = primitive.mesh.indices;
            for (std::size_t corner = 0; corner + 2 < indices.size(); corner += 3) {
                Triangle triangle{.color = color, .glow = glow};
                bool valid = true;
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t vertex = indices[corner + static_cast<std::size_t>(k)];
                    if (vertex >= vertices.size()) {
                        valid = false;
                        break;
                    }
                    triangle.corners[k] = math::TransformPoint(world, vertices[vertex].position);
                    low = glm::min(low, triangle.corners[k]);
                    high = glm::max(high, triangle.corners[k]);
                }
                if (valid) {
                    triangles.push_back(triangle);
                }
            }
        }
    }
    if (triangles.empty()) {
        return pixels;
    }

    // Seen from the front, to the right and above, the whole bounding sphere in view (orthographic).
    const glm::vec3 center = (low + high) * 0.5f;
    const float radius = std::max(glm::length(high - low) * 0.5f, 1e-4f);
    const glm::vec3 back = glm::normalize(glm::vec3(0.75f, 0.6f, 1.0f));
    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), back));
    const glm::vec3 up = glm::cross(back, right);
    const glm::vec3 light = glm::normalize(glm::vec3(-0.4f, 0.8f, 0.6f));

    const std::uint32_t big = size * kSupersample;
    const float scale = static_cast<float>(big) * 0.5f / (radius * 1.02f);
    std::vector<float> depth(static_cast<std::size_t>(big) * big, std::numeric_limits<float>::max());
    std::vector<glm::vec4> color(static_cast<std::size_t>(big) * big, glm::vec4(0.0f));

    for (const Triangle& triangle : triangles) {
        glm::vec3 screen[3];
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 offset = triangle.corners[k] - center;
            screen[k] = {static_cast<float>(big) * 0.5f + glm::dot(offset, right) * scale,
                         static_cast<float>(big) * 0.5f - glm::dot(offset, up) * scale, -glm::dot(offset, back)};
        }
        const float area = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                           (screen[2].x - screen[0].x) * (screen[1].y - screen[0].y);
        if (std::abs(area) < 1e-12f) {
            continue;
        }
        // Lit on both sides: the normal faces the viewer.
        glm::vec3 normal =
            glm::cross(triangle.corners[1] - triangle.corners[0], triangle.corners[2] - triangle.corners[0]);
        const float length = glm::length(normal);
        if (length < 1e-20f) {
            continue;
        }
        normal /= length;
        if (glm::dot(normal, back) < 0.0f) {
            normal = -normal;
        }
        const float diffuse = std::max(glm::dot(normal, light), 0.0f);
        const float sky = 0.5f + 0.5f * normal.y;
        const float rim = std::pow(1.0f - std::max(glm::dot(normal, back), 0.0f), 3.0f);
        const glm::vec3 shaded =
            triangle.color * (0.85f * diffuse + 0.3f * sky) + glm::vec3(0.12f * rim) + triangle.glow;

        const int minX = std::max(0, static_cast<int>(std::floor(std::min({screen[0].x, screen[1].x, screen[2].x}))));
        const int maxX = std::min(static_cast<int>(big) - 1,
                                  static_cast<int>(std::ceil(std::max({screen[0].x, screen[1].x, screen[2].x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({screen[0].y, screen[1].y, screen[2].y}))));
        const int maxY = std::min(static_cast<int>(big) - 1,
                                  static_cast<int>(std::ceil(std::max({screen[0].y, screen[1].y, screen[2].y}))));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = ((screen[2].x - screen[1].x) * (py - screen[1].y) -
                                  (screen[2].y - screen[1].y) * (px - screen[1].x)) /
                                 area;
                const float w1 = ((screen[0].x - screen[2].x) * (py - screen[2].y) -
                                  (screen[0].y - screen[2].y) * (px - screen[2].x)) /
                                 area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                const float z = w0 * screen[0].z + w1 * screen[1].z + w2 * screen[2].z;
                const std::size_t at = static_cast<std::size_t>(y) * big + static_cast<std::size_t>(x);
                if (z < depth[at]) {
                    depth[at] = z;
                    color[at] = glm::vec4(shaded, 1.0f);
                }
            }
        }
    }

    // Averaged down, in linear light, then encoded.
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            glm::vec4 sum{0.0f};
            for (std::uint32_t sy = 0; sy < kSupersample; ++sy) {
                for (std::uint32_t sx = 0; sx < kSupersample; ++sx) {
                    const glm::vec4 sample =
                        color[static_cast<std::size_t>(y * kSupersample + sy) * big + (x * kSupersample + sx)];
                    sum += glm::vec4(glm::vec3(sample) * sample.a, sample.a);
                }
            }
            sum /= static_cast<float>(kSupersample * kSupersample);
            const glm::vec3 rgb = sum.a > 0.0f ? glm::vec3(sum) / sum.a : glm::vec3(0.0f);
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
            pixel[0] = static_cast<std::uint8_t>(ToSrgb(rgb.r) * 255.0f + 0.5f);
            pixel[1] = static_cast<std::uint8_t>(ToSrgb(rgb.g) * 255.0f + 0.5f);
            pixel[2] = static_cast<std::uint8_t>(ToSrgb(rgb.b) * 255.0f + 0.5f);
            pixel[3] = static_cast<std::uint8_t>(sum.a * 255.0f + 0.5f);
        }
    }
    return pixels;
}

std::vector<std::uint8_t> Shrink(const resources::ImageData& image, std::uint32_t size) {
    if ((image.format != resources::PixelFormat::Rgba8Unorm && image.format != resources::PixelFormat::Rgba8Srgb) ||
        image.width == 0 || image.height == 0 || size == 0 ||
        image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4) {
        return {};
    }
    // The largest square at the center, averaged over the pixels each thumbnail pixel covers.
    const std::uint32_t side = std::min(image.width, image.height);
    const std::uint32_t left = (image.width - side) / 2;
    const std::uint32_t top = (image.height - side) / 2;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4);
    const auto* source = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
    for (std::uint32_t y = 0; y < size; ++y) {
        const std::uint32_t y0 = top + y * side / size;
        const std::uint32_t y1 = std::max(top + (y + 1) * side / size, y0 + 1);
        for (std::uint32_t x = 0; x < size; ++x) {
            const std::uint32_t x0 = left + x * side / size;
            const std::uint32_t x1 = std::max(left + (x + 1) * side / size, x0 + 1);
            std::uint32_t sum[4] = {0, 0, 0, 0};
            std::uint32_t count = 0;
            for (std::uint32_t sy = y0; sy < y1; ++sy) {
                for (std::uint32_t sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* pixel = &source[(static_cast<std::size_t>(sy) * image.width + sx) * 4];
                    for (int channel = 0; channel < 4; ++channel) {
                        sum[channel] += pixel[channel];
                    }
                    ++count;
                }
            }
            for (int channel = 0; channel < 4; ++channel) {
                pixels[(static_cast<std::size_t>(y) * size + x) * 4 + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(sum[channel] / count);
            }
        }
    }
    return pixels;
}

} // namespace editor
