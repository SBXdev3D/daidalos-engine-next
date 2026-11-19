// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/GltfMaterials.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace daidalos;

namespace {

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

/// Writes edits into a copy of a test model and loads it back as the engine does.
resources::MaterialData RoundTrip(const char* model, const resources::MaterialData& values) {
    const std::filesystem::path source = std::filesystem::path(DAI_TEST_ASSET_DIR) / model;
    // One file per test: the tests may run at once, as CTest runs them.
    const std::filesystem::path copy = std::filesystem::temp_directory_path() /
                                       (std::string("daidalos_gltf_materials_") +
                                        testing::UnitTest::GetInstance()->current_test_info()->name() + ".gltf");
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing);
    const scene::MaterialEdit edit{.index = 0, .values = values};
    const core::Result<void> saved = scene::SaveGltfMaterials(copy, std::span(&edit, 1));
    EXPECT_TRUE(saved.has_value()) << core::Format(saved.error());
    core::Result<resources::ModelData> loaded = resources::LoadModelFile(copy);
    EXPECT_TRUE(loaded.has_value()) << core::Format(loaded.error());
    std::filesystem::remove(copy);
    return loaded && !loaded->materials.empty() ? loaded->materials[0] : resources::MaterialData{};
}

TEST(GltfMaterialsTest, EditedValuesLoadBack) {
    resources::MaterialData values;
    values.baseColorFactor = {0.25f, 0.5f, 0.75f, 1.0f};
    values.metallicFactor = 0.0f;
    values.roughnessFactor = 0.35f;
    values.emissiveFactor = {120.0f, 60.0f, 0.0f};
    values.emissiveLight = false;
    values.surface = "metal";
    values.porosity = 0.1f;
    values.heightScale = 0.01f;
    values.clearcoat = 0.7f;
    values.clearcoatRoughness = 0.1f;
    values.sheenColor = {0.2f, 0.1f, 0.05f};
    values.sheenRoughness = 0.6f;
    values.doubleSided = true;
    values.alphaCutoff = 0.4f;
    const resources::MaterialData read = RoundTrip("quad.gltf", values);
    EXPECT_EQ(read.baseColorFactor, values.baseColorFactor);
    EXPECT_FLOAT_EQ(read.roughnessFactor, 0.35f);
    EXPECT_NEAR(read.emissiveFactor.r, 120.0f, 1e-3f) << "Strength over 1 through KHR_materials_emissive_strength";
    EXPECT_NEAR(read.emissiveFactor.g, 60.0f, 1e-3f);
    EXPECT_FALSE(read.emissiveLight);
    EXPECT_EQ(read.surface, "metal");
    EXPECT_FLOAT_EQ(read.porosity, 0.1f);
    EXPECT_FLOAT_EQ(read.heightScale, 0.01f);
    EXPECT_FLOAT_EQ(read.clearcoat, 0.7f);
    EXPECT_FLOAT_EQ(read.clearcoatRoughness, 0.1f);
    EXPECT_EQ(read.sheenColor, values.sheenColor);
    EXPECT_FLOAT_EQ(read.sheenRoughness, 0.6f);
    EXPECT_TRUE(read.doubleSided);
    EXPECT_FLOAT_EQ(read.alphaCutoff, 0.4f);
}

TEST(GltfMaterialsTest, GlassTurnedOpaqueLosesItsExtensions) {
    resources::MaterialData values;
    values.transmission = 0.0f;
    const resources::MaterialData read = RoundTrip("glass.gltf", values);
    EXPECT_FLOAT_EQ(read.transmission, 0.0f);
    EXPECT_FLOAT_EQ(read.ior, 1.5f);
    EXPECT_FLOAT_EQ(read.thickness, 0.0f);

    const scene::MaterialEdit edit{.index = 0, .values = values};
    const core::Result<std::string> written = scene::WriteGltfMaterials(
        ReadText(std::filesystem::path(DAI_TEST_ASSET_DIR) / "glass.gltf"), std::span(&edit, 1));
    ASSERT_TRUE(written.has_value()) << core::Format(written.error());
    EXPECT_EQ(written->find("KHR_materials_transmission"), std::string::npos) << "Nor declared as used";
}

TEST(GltfMaterialsTest, GlassKeepsWhatItIs) {
    resources::MaterialData values;
    values.transmission = 0.6f;
    values.ior = 1.33f;
    values.thickness = 0.05f;
    values.attenuationColor = {0.5f, 0.6f, 0.7f};
    values.attenuationDistance = 0.2f;
    const resources::MaterialData read = RoundTrip("glass.gltf", values);
    EXPECT_FLOAT_EQ(read.transmission, 0.6f);
    EXPECT_FLOAT_EQ(read.ior, 1.33f);
    EXPECT_FLOAT_EQ(read.thickness, 0.05f);
    EXPECT_EQ(read.attenuationColor, values.attenuationColor);
    EXPECT_FLOAT_EQ(read.attenuationDistance, 0.2f);
}

TEST(GltfMaterialsTest, RefusesWhatIsNotThere) {
    const scene::MaterialEdit edit{.index = 3};
    EXPECT_FALSE(scene::WriteGltfMaterials(R"({"asset": {"version": "2.0"}, "materials": [{}]})", std::span(&edit, 1)));
    EXPECT_FALSE(scene::WriteGltfMaterials(R"({"materials": []})", {}));
    EXPECT_FALSE(scene::SaveGltfMaterials("model.glb", {}));
}

} // namespace
