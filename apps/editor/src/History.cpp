// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "History.hpp"

#include <algorithm>
#include <cmath>

using namespace daidalos;

namespace editor {

bool SameValues(const resources::MaterialData& a, const resources::MaterialData& b) {
    const auto distance = [](float x, float y) { return (std::isinf(x) && std::isinf(y)) || x == y; };
    return a.baseColorFactor == b.baseColorFactor && a.emissiveFactor == b.emissiveFactor &&
           a.emissiveLight == b.emissiveLight && a.surface == b.surface && a.metallicFactor == b.metallicFactor &&
           a.roughnessFactor == b.roughnessFactor && a.normalScale == b.normalScale &&
           a.occlusionStrength == b.occlusionStrength && a.alphaCutoff == b.alphaCutoff &&
           a.doubleSided == b.doubleSided && a.transmission == b.transmission && a.ior == b.ior &&
           a.porosity == b.porosity && a.heightScale == b.heightScale && a.clearcoat == b.clearcoat &&
           a.clearcoatRoughness == b.clearcoatRoughness && a.sheenColor == b.sheenColor &&
           a.sheenRoughness == b.sheenRoughness && a.thickness == b.thickness &&
           a.attenuationColor == b.attenuationColor && distance(a.attenuationDistance, b.attenuationDistance);
}

bool Document::operator==(const Document& other) const {
    return level == other.level && sequencePath == other.sequencePath && sequence == other.sequence &&
           std::ranges::equal(materials, other.materials, [](const auto& a, const auto& b) {
               return a.first == b.first && SameValues(a.second, b.second);
           });
}

} // namespace editor
