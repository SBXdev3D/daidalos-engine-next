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

TEST(LoadModelFile, RigidMeshHasNoSkin) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "quad.gltf");
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->primitives[0].mesh.skin.empty());
    EXPECT_TRUE(model->skins.empty());
    EXPECT_TRUE(model->animations.empty());
    EXPECT_FALSE(model->nodes[1].skin.has_value());
}

TEST(LoadModelFile, ReadsTheJointsAndWeightsOfEachVertex) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "skinned.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    const std::vector<resources::SkinWeights>& skin = model->primitives[0].mesh.skin;
    ASSERT_EQ(skin.size(), 4U);
    EXPECT_EQ(skin[0].joints[0], 0U);
    EXPECT_EQ(skin[2].joints[0], 1U);
    EXPECT_EQ(skin[2].weights, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    // Its weights add up to 2 in the file.
    EXPECT_EQ(skin[3].weights, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST(LoadModelFile, SkinJointsAreNodesOfTheModel) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "skinned.gltf");
    ASSERT_TRUE(model.has_value());
    // The model lists nodes parents first, depth first: Character, Hip, Hand, then Body.
    ASSERT_EQ(model->nodes.size(), 4U);
    EXPECT_EQ(model->nodes[1].name, "Hip");
    EXPECT_EQ(model->nodes[3].name, "Body");
    EXPECT_EQ(model->nodes[3].skin, 0U);
    ASSERT_EQ(model->skins.size(), 1U);
    EXPECT_EQ(model->skins[0].joints, (std::vector<std::uint32_t>{1, 2}));
    ASSERT_EQ(model->skins[0].inverseBindMatrices.size(), 2U);
    EXPECT_EQ(model->skins[0].inverseBindMatrices[0], glm::mat4(1.0f));
    EXPECT_EQ(model->skins[0].inverseBindMatrices[1][3], glm::vec4(0.0f, -1.0f, 0.0f, 1.0f));
}

TEST(LoadModelFile, ReadsAnimationsWithTheirKeys) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "skinned.gltf");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->animations.size(), 3U);
    const resources::AnimationData& lift = model->animations[0];
    EXPECT_EQ(lift.name, "lift");
    // The channel of the node outside the scene is left out.
    ASSERT_EQ(lift.channels.size(), 1U);
    const resources::AnimationChannel& channel = lift.channels[0];
    EXPECT_EQ(channel.node, 2U);
    EXPECT_EQ(channel.path, resources::AnimationPath::Translation);
    EXPECT_EQ(channel.interpolation, resources::AnimationInterpolation::Linear);
    EXPECT_EQ(channel.times, (std::vector<float>{0.0f, 1.0f, 2.0f}));
    EXPECT_EQ(channel.values[2], glm::vec4(0.0f, 3.0f, 0.0f, 0.0f));

    const resources::AnimationChannel& snap = model->animations[1].channels.at(0);
    EXPECT_EQ(snap.path, resources::AnimationPath::Rotation);
    EXPECT_EQ(snap.interpolation, resources::AnimationInterpolation::Step);
    EXPECT_NEAR(snap.values[1].y, 0.7071068f, 1e-6f);
    EXPECT_NEAR(snap.values[1].w, 0.7071068f, 1e-6f);
}

TEST(LoadModelFile, CubicSplineKeepsOnlyTheValues) {
    const core::Result<ModelData> model = resources::LoadModelFile(kAssets / "skinned.gltf");
    ASSERT_TRUE(model.has_value());
    const resources::AnimationChannel& spline = model->animations[2].channels.at(0);
    EXPECT_EQ(spline.interpolation, resources::AnimationInterpolation::Linear);
    ASSERT_EQ(spline.values.size(), 2U);
    EXPECT_EQ(spline.values[0], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(spline.values[1], glm::vec4(2.0f, 0.0f, 0.0f, 0.0f));
}

namespace {

// A square facing +Z, with u along +X and v along -Y, as in an image: its texture is upright.
resources::MeshData UprightSquare() {
    resources::MeshData mesh;
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    mesh.vertices = {{{-1.0f, -1.0f, 0.0f}, normal, {}, {0.0f, 1.0f}},
                     {{1.0f, -1.0f, 0.0f}, normal, {}, {1.0f, 1.0f}},
                     {{1.0f, 1.0f, 0.0f}, normal, {}, {1.0f, 0.0f}},
                     {{-1.0f, 1.0f, 0.0f}, normal, {}, {0.0f, 0.0f}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

} // namespace

TEST(GenerateTangents, FollowTheGltfConvention) {
    resources::MeshData mesh = UprightSquare();
    resources::GenerateTangents(mesh);
    ASSERT_EQ(mesh.vertices.size(), 4U);
    for (const resources::Vertex& vertex : mesh.vertices) {
        // The tangent follows u. The bitangent, cross(normal, tangent) * w, points up the image: +Y.
        EXPECT_NEAR(vertex.tangent.x, 1.0f, 1e-5f);
        EXPECT_NEAR(vertex.tangent.y, 0.0f, 1e-5f);
        EXPECT_NEAR(vertex.tangent.z, 0.0f, 1e-5f);
        EXPECT_EQ(vertex.tangent.w, 1.0f);
    }
}

TEST(GenerateTangents, MirroredTextureFlipsTheHandedness) {
    resources::MeshData mesh = UprightSquare();
    for (resources::Vertex& vertex : mesh.vertices) {
        vertex.uv.y = 1.0f - vertex.uv.y;
    }
    resources::GenerateTangents(mesh);
    for (const resources::Vertex& vertex : mesh.vertices) {
        EXPECT_NEAR(vertex.tangent.x, 1.0f, 1e-5f);
        EXPECT_EQ(vertex.tangent.w, -1.0f);
    }
}

TEST(GenerateTangents, SplitsVerticesThatDisagree) {
    // Two squares sharing an edge, the second with its texture mirrored: the shared vertices need two
    // tangents each, so they are split.
    resources::MeshData mesh = UprightSquare();
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back({{3.0f, -1.0f, 0.0f}, normal, {}, {0.0f, 1.0f}});
    mesh.vertices.push_back({{3.0f, 1.0f, 0.0f}, normal, {}, {0.0f, 0.0f}});
    mesh.indices.insert(mesh.indices.end(), {1, 4, 5, 1, 5, 2});
    resources::GenerateTangents(mesh);

    EXPECT_EQ(mesh.vertices.size(), 8U);
    for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner) {
        const glm::vec4& tangent = mesh.vertices[mesh.indices[corner]].tangent;
        EXPECT_NEAR(tangent.x, corner < 6 ? 1.0f : -1.0f, 1e-5f) << "corner " << corner;
    }
}
