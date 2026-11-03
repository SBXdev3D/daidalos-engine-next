// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/math/Aabb.hpp>

#include <glm/common.hpp>
#include <glm/vec4.hpp>

namespace daidalos::math {

Aabb Aabb::FromCenterExtents(const glm::vec3& center, const glm::vec3& extents) {
    return Aabb{center - extents, center + extents};
}

bool Aabb::IsEmpty() const {
    return min.x > max.x || min.y > max.y || min.z > max.z;
}

glm::vec3 Aabb::Center() const {
    return (min + max) * 0.5f;
}

glm::vec3 Aabb::Extents() const {
    return (max - min) * 0.5f;
}

void Aabb::Expand(const glm::vec3& point) {
    min = glm::min(min, point);
    max = glm::max(max, point);
}

void Aabb::Expand(const Aabb& other) {
    if (other.IsEmpty()) {
        return;
    }
    min = glm::min(min, other.min);
    max = glm::max(max, other.max);
}

bool Aabb::Contains(const glm::vec3& point) const {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z &&
           point.z <= max.z;
}

bool Aabb::Intersects(const Aabb& other) const {
    return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
}

Aabb TransformAabb(const Aabb& box, const glm::mat4& matrix) {
    if (box.IsEmpty()) {
        return Aabb{};
    }
    // Arvo's method: the transformed center, plus the extents projected on each axis by the absolute
    // value of the linear part.
    const glm::vec3 center = glm::vec3(matrix * glm::vec4(box.Center(), 1.0f));
    const glm::vec3 extents = box.Extents();
    glm::vec3 newExtents(0.0f);
    for (int column = 0; column < 3; ++column) {
        newExtents += glm::abs(glm::vec3(matrix[column])) * extents[column];
    }
    return Aabb::FromCenterExtents(center, newExtents);
}

} // namespace daidalos::math
