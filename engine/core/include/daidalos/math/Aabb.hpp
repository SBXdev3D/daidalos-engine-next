// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <limits>

namespace daidalos::math {

/// Axis-aligned bounding box. The default box is empty (min > max), so that expanding it by a first
/// point gives a box around that point.
struct Aabb {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    [[nodiscard]] static Aabb FromCenterExtents(const glm::vec3& center, const glm::vec3& extents);

    /// True when the box contains no point. A box with min == max contains one point and is not empty.
    [[nodiscard]] bool IsEmpty() const;
    [[nodiscard]] glm::vec3 Center() const;
    /// Half the size along each axis.
    [[nodiscard]] glm::vec3 Extents() const;

    void Expand(const glm::vec3& point);
    void Expand(const Aabb& other);

    /// Points on the boundary are inside.
    [[nodiscard]] bool Contains(const glm::vec3& point) const;
    /// Boxes that only touch intersect.
    [[nodiscard]] bool Intersects(const Aabb& other) const;

    bool operator==(const Aabb&) const = default;
};

/// Smallest axis-aligned box around the transformed box. Empty boxes stay empty.
[[nodiscard]] Aabb TransformAabb(const Aabb& box, const glm::mat4& matrix);

} // namespace daidalos::math
