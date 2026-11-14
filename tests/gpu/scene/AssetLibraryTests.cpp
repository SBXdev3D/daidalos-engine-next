// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/animation/Animation.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Systems.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace daidalos;
using scene::AssetLibrary;
using scene::Model;

namespace {

class AssetLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::Result<platform::Platform> created = platform::Platform::Create();
        if (!created) {
            GTEST_SKIP() << core::Format(created.error());
        }
        m_platform.emplace(std::move(*created));
        platform::WindowConfig config;
        config.width = 64;
        config.height = 64;
        config.vulkan = true;
        core::Result<platform::Window> window = platform::Window::Create(*m_platform, config);
        if (!window) {
            GTEST_SKIP() << core::Format(window.error());
        }
        m_window.emplace(std::move(*window));
        core::Result<renderer::Renderer> target =
            renderer::Renderer::Create(*m_window, {.validation = true, .synchronizationValidation = true});
        if (!target) {
            ASSERT_EQ(target.error().code, core::ErrorCode::NotSupported) << core::Format(target.error());
            GTEST_SKIP() << core::Format(target.error());
        }
        m_renderer.emplace(std::move(*target));
    }

    void TearDown() override {
        if (m_renderer.has_value()) {
            EXPECT_EQ(m_renderer->Device().ValidationErrorCount(), 0U);
        }
    }

    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
    std::optional<renderer::Renderer> m_renderer;
};

constexpr const char* kAssets = DAI_TEST_ASSET_DIR;

} // namespace

TEST_F(AssetLibraryTest, RepeatedRequestsReturnTheSameModel) {
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const Model*> first = library.LoadModel("quad.gltf");
    ASSERT_TRUE(first.has_value()) << core::Format(first.error());
    const core::Result<const Model*> second = library.LoadModel("./sub/../quad.gltf");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
    EXPECT_EQ(library.ModelCount(), 1U);
}

TEST_F(AssetLibraryTest, DescribesWhatItHoldsByPath) {
    AssetLibrary library(*m_renderer, kAssets);
    ASSERT_TRUE(library.LoadModel("quad.gltf").has_value());
    const std::vector<AssetLibrary::AssetInfo> assets = library.Describe();
    ASSERT_FALSE(assets.empty());
    EXPECT_EQ(assets.front().kind, AssetLibrary::AssetInfo::Kind::Model);
    EXPECT_EQ(assets.front().path, "quad.gltf");
    EXPECT_EQ(assets.size(), 1 + library.TextureCount());
    EXPECT_EQ(library.PendingLoadCount(), 0U);
}

TEST_F(AssetLibraryTest, TextureSharedByTwoModelsIsLoadedOnce) {
    AssetLibrary library(*m_renderer, kAssets);
    ASSERT_TRUE(library.LoadModel("quad.gltf").has_value());
    ASSERT_TRUE(library.LoadModel("second-quad.gltf").has_value());
    EXPECT_EQ(library.ModelCount(), 2U);
    EXPECT_EQ(library.TextureCount(), 1U);
}

TEST_F(AssetLibraryTest, MissingTextureFailsTheModelAndNamesTheFile) {
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const Model*> model = library.LoadModel("missing-texture.gltf");
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(model.error().message.find("no-such-image.png"), std::string::npos);
    EXPECT_EQ(library.ModelCount(), 0U);
}

TEST_F(AssetLibraryTest, MissingModelIsNotFound) {
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const Model*> model = library.LoadModel("no-such-model.gltf");
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, core::ErrorCode::NotFound);
}

TEST_F(AssetLibraryTest, FontIsBakedOnceAndUploaded) {
    AssetLibrary library(*m_renderer, DAI_PROJECT_ASSET_DIR);
    const core::Result<const ui::Font*> font = library.LoadFont("Fonts/Rajdhani-Medium.ttf");
    ASSERT_TRUE(font.has_value()) << core::Format(font.error());
    EXPECT_FALSE((*font)->atlas.IsNull());
    EXPECT_NE((*font)->data.Find(U'A'), nullptr);
    const core::Result<const ui::Font*> again = library.LoadFont("Fonts/Rajdhani-Medium.ttf");
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *font);
}

TEST_F(AssetLibraryTest, MissingFontIsNotFound) {
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const ui::Font*> font = library.LoadFont("no-such-font.ttf");
    ASSERT_FALSE(font.has_value());
    EXPECT_EQ(font.error().code, core::ErrorCode::NotFound);
}

TEST_F(AssetLibraryTest, ReleasedAssetsAreSkippedByLaterFrames) {
    ecs::World world;
    const ecs::Entity camera = world.Create();
    ecs::LocalTransform transform;
    transform.translation = {0.0f, 0.0f, 5.0f};
    world.Add<ecs::LocalTransform>(camera, transform);
    world.Add<scene::Camera>(camera);

    renderer::SceneView view;
    const auto renderFrames = [&] {
        ecs::PropagateTransforms(world);
        scene::ExtractScene(world, view);
        for (int frame = 0; frame < 3; ++frame) {
            ASSERT_TRUE(m_renderer->Render(view).has_value());
        }
    };
    {
        AssetLibrary library(*m_renderer, kAssets);
        const core::Result<const Model*> model = library.LoadModel("quad.gltf");
        ASSERT_TRUE(model.has_value());
        ASSERT_TRUE(scene::Instantiate(world, **model).has_value());
        renderFrames();
        EXPECT_EQ(view.draws.size(), 1U);
    }
    renderFrames();
}

TEST(Instantiate, BuildsTheHierarchyWithOneEntityPerExtraPart) {
    Model model;
    model.parts = {{renderer::MeshHandle(1, 1), renderer::MaterialHandle(1, 1)},
                   {renderer::MeshHandle(2, 1), renderer::MaterialHandle(2, 1)}};
    Model::Node& parent = model.nodes.emplace_back();
    parent.local.translation = {1.0f, 0.0f, 0.0f};
    Model::Node& child = model.nodes.emplace_back();
    child.parent = 0;
    child.local.translation = {0.0f, 2.0f, 0.0f};
    child.parts = {0, 1};

    ecs::World world;
    const core::Result<ecs::Entity> root = scene::Instantiate(world, model);
    ASSERT_TRUE(root.has_value());
    ecs::PropagateTransforms(world);

    renderer::SceneView view;
    scene::ExtractScene(world, view);
    ASSERT_EQ(view.draws.size(), 2U);
    for (const renderer::DrawItem& draw : view.draws) {
        EXPECT_EQ(glm::vec3(draw.world[3]), glm::vec3(1.0f, 2.0f, 0.0f));
    }

    EXPECT_TRUE(world.Destroy(*root));
    EXPECT_EQ(world.Count<scene::MeshRenderer>(), 0U);
}

TEST_F(AssetLibraryTest, LightsOfAModelBecomeLightsOfItsEntities) {
    // KHR_lights_punctual (ADR-0043): a spot with its own range, and a point light without one, which gets
    // the range where it falls to 0.05 lux, 20 m for 20 candela.
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const Model*> model = library.LoadModel("lamp.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    ecs::World world;
    ASSERT_TRUE(scene::Instantiate(world, **model).has_value());
    std::vector<scene::Light> lights;
    world.Each<scene::Light>([&](ecs::Entity /*entity*/, const scene::Light& light) { lights.push_back(light); });
    ASSERT_EQ(lights.size(), 2U);
    std::ranges::sort(lights, {}, &scene::Light::intensity);
    EXPECT_EQ(lights[0].type, renderer::LightType::Point);
    EXPECT_FLOAT_EQ(lights[0].intensity, 20.0f);
    EXPECT_NEAR(lights[0].range, 20.0f, 1e-4f);
    EXPECT_EQ(lights[1].type, renderer::LightType::Spot);
    EXPECT_FLOAT_EQ(lights[1].intensity, 250.0f);
    EXPECT_FLOAT_EQ(lights[1].range, 9.0f);
    EXPECT_FLOAT_EQ(lights[1].innerAngle, 0.2f);
    EXPECT_FLOAT_EQ(lights[1].outerAngle, 0.5f);
}

TEST_F(AssetLibraryTest, SkinnedModelIsAnimatedAndDrawn) {
    AssetLibrary library(*m_renderer, kAssets);
    const core::Result<const Model*> model = library.LoadModel("skinned.gltf");
    ASSERT_TRUE(model.has_value()) << core::Format(model.error());
    ASSERT_EQ((*model)->skins.size(), 1U);
    ASSERT_NE((*model)->clips, nullptr);
    EXPECT_EQ((*model)->clips->clips.size(), 3U);

    ecs::World world;
    const core::Result<ecs::Entity> root = scene::Instantiate(world, **model);
    ASSERT_TRUE(root.has_value());
    auto* animator = world.TryGet<animation::Animator>(*root);
    ASSERT_NE(animator, nullptr);
    ecs::Entity body;
    ecs::Entity hand;
    world.Each<scene::NodeName>([&](ecs::Entity entity, const scene::NodeName& name) {
        body = name.value == "Body" ? entity : body;
        hand = name.value == "Hand" ? entity : hand;
    });
    const auto* skinned = world.TryGet<scene::SkinnedMeshRenderer>(body);
    ASSERT_NE(skinned, nullptr);
    ASSERT_EQ(skinned->joints.size(), 2U);
    EXPECT_EQ(skinned->joints[1], hand);
    EXPECT_FALSE(world.Has<scene::MeshRenderer>(body));

    ASSERT_TRUE(animator->Play("lift"));
    animation::UpdateAnimators(world, 1.0f);
    EXPECT_EQ(world.Get<ecs::LocalTransform>(hand).translation, glm::vec3(0.0f, 2.0f, 0.0f));

    const ecs::Entity camera = world.Create();
    ecs::LocalTransform transform;
    transform.translation = {0.0f, 0.0f, 5.0f};
    world.Add<ecs::LocalTransform>(camera, transform);
    world.Add<scene::Camera>(camera);
    ecs::PropagateTransforms(world);
    renderer::SceneView view;
    scene::ExtractScene(world, view);
    EXPECT_EQ(view.jointMatrices.size(), 2U);
    for (int frame = 0; frame < 3; ++frame) {
        ASSERT_TRUE(m_renderer->Render(view).has_value());
    }
}
