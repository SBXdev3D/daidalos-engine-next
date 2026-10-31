// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/resources/ModelData.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace daidalos;
using resources::ModelData;

namespace {

const std::filesystem::path kAssets = DAI_TEST_ASSET_DIR;

} // namespace

TEST(LoadModelFile, ReadsTheGeometry) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "quad.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    ASSERT_EQ(model->primitives.size(), 1U);
    const resources::MeshData& mesh = model->primitives[0].mesh;
    ASSERT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(mesh.vertices[1].position, glm::vec3(1.0f, -1.0f, 0.0f));
    EXPECT_EQ(mesh.vertices[1].normal, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(mesh.vertices[1].uv, glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(mesh.vertices[1].tangent, glm::vec4(0.0f));
}

TEST(LoadModelFile, KeepsTheNodeHierarchyParentsFirst) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "quad.gltf");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->nodes.size(), 2U);
    EXPECT_EQ(model->nodes[0].name, "Parent");
    EXPECT_FALSE(model->nodes[0].parent.has_value());
    EXPECT_EQ(model->nodes[0].local.translation, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(model->nodes[1].name, "Child");
    EXPECT_EQ(model->nodes[1].parent, 0U);
    EXPECT_EQ(model->nodes[1].local.scale, glm::vec3(2.0f));
    EXPECT_EQ(model->nodes[1].primitives, (std::vector<std::uint32_t>{0}));
}

TEST(LoadModelFile, ReadsTheMaterialWithAllItsValues) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "quad.gltf");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->materials.size(), 1U);
    const resources::MaterialData& material = model->materials[0];
    EXPECT_EQ(material.name, "Checker");
    EXPECT_EQ(material.baseColorFactor, glm::vec4(1.0f, 0.5f, 0.25f, 1.0f));
    EXPECT_FLOAT_EQ(material.metallicFactor, 0.1f);
    EXPECT_FLOAT_EQ(material.roughnessFactor, 0.9f);
    EXPECT_FLOAT_EQ(material.alphaCutoff, 0.25f);
    EXPECT_TRUE(material.doubleSided);
    EXPECT_EQ(model->primitives[0].material, 0U);
}

TEST(LoadModelFile, ImagesAreNotDecodedButLocated) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "quad.gltf");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->images.size(), 1U);
    const resources::ImageSource& image = model->images[0];
    EXPECT_EQ(image.use, resources::TextureUse::Color);
    EXPECT_EQ(image.file.filename(), "checker.png");
    EXPECT_TRUE(image.embedded.empty());
    EXPECT_EQ(model->materials[0].baseColorImage, 0U);
}

TEST(LoadModelFile, MissingNormalsAreComputedFromTheTriangles) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "no-normals.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    for (const resources::Vertex& vertex : model->primitives[0].mesh.vertices) {
        EXPECT_FLOAT_EQ(glm::dot(vertex.normal, glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f);
    }
}

TEST(LoadModelFile, MissingFileIsNotFound) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "no-such-model.gltf");
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(model.error().message.find("no-such-model.gltf"), std::string::npos);
}

TEST(LoadModelFile, InvalidFileIsAParseErrorThatNamesTheFile) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "broken.gltf");
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, core::ErrorCode::ParseError);
    EXPECT_NE(model.error().message.find("broken.gltf"), std::string::npos);
}

TEST(LoadModelFile, NodeCycleIsAnError) {
    EXPECT_FALSE(resources::LoadModelFile(kAssets / "cycle.gltf").has_value());
}
