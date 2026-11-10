// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <glm/vec4.hpp>
#include <gtest/gtest.h>

#include <vector>

#include "RenderGraph.hpp"

namespace {

using namespace daidalos;
using renderer::PlannedPass;
using renderer::RenderGraph;
using rhi::Access;

const rhi::TextureHandle kColor{1, 1};
const rhi::TextureHandle kDepth{2, 1};
const rhi::TextureHandle kSecondColor{3, 1};
const rhi::TextureHandle kThirdColor{4, 1};
const rhi::BufferHandle kBuffer{1, 1};

void Nothing(rhi::CommandList& /*commands*/) {}

const rhi::TextureUse* Find(const PlannedPass& pass, rhi::TextureHandle texture) {
    for (const rhi::TextureUse& use : pass.textures) {
        if (use.texture == texture) {
            return &use;
        }
    }
    return nullptr;
}

} // namespace

TEST(RenderGraph, RenderingTargetsAreDeclaredAsWritten) {
    RenderGraph graph;
    graph.AddPass({.name = "Meshes", .rendering = rhi::RenderingDesc{.colorTarget = kColor, .depthTarget = kDepth}},
                  Nothing);
    const std::vector<PlannedPass> planned = graph.Plan();
    ASSERT_EQ(planned.size(), 1U);
    ASSERT_NE(Find(planned[0], kColor), nullptr);
    EXPECT_EQ(Find(planned[0], kColor)->access, Access::ColorTarget);
    EXPECT_FALSE(Find(planned[0], kColor)->discard);
    ASSERT_NE(Find(planned[0], kDepth), nullptr);
    EXPECT_EQ(Find(planned[0], kDepth)->access, Access::DepthTarget);
}

TEST(RenderGraph, ClearedTargetsLoseTheirContent) {
    RenderGraph graph;
    graph.AddPass({.name = "Prepass",
                   .rendering = rhi::RenderingDesc{.colorTarget = kColor,
                                                   .clearColor = glm::vec4(0.0f),
                                                   .moreColorTargets = {{kSecondColor, glm::vec4(0.0f)}, {kThirdColor}},
                                                   .depthTarget = kDepth}},
                  Nothing);
    const PlannedPass pass = graph.Plan().front();
    EXPECT_TRUE(Find(pass, kColor)->discard);
    ASSERT_NE(Find(pass, kSecondColor), nullptr);
    EXPECT_EQ(Find(pass, kSecondColor)->access, Access::ColorTarget);
    EXPECT_TRUE(Find(pass, kSecondColor)->discard);
    EXPECT_FALSE(Find(pass, kThirdColor)->discard); // Loaded: no clear.
    EXPECT_FALSE(Find(pass, kDepth)->discard);
}

TEST(RenderGraph, UsesOfOneResourceInAPassAreMerged) {
    RenderGraph graph;
    graph.AddPass({.name = "Read twice",
                   .textures = {{kColor, Access::FragmentShaderRead, true}, {kColor, Access::ComputeShaderRead}},
                   .buffers = {{kBuffer, Access::VertexShaderRead}, {kBuffer, Access::IndirectRead}}},
                  Nothing);
    const PlannedPass pass = graph.Plan().front();
    ASSERT_EQ(pass.textures.size(), 1U);
    EXPECT_EQ(pass.textures[0].access, Access::FragmentShaderRead | Access::ComputeShaderRead);
    EXPECT_FALSE(pass.textures[0].discard); // Only one use dropped the content.
    ASSERT_EQ(pass.buffers.size(), 1U);
    EXPECT_EQ(pass.buffers[0].access, Access::VertexShaderRead | Access::IndirectRead);
}

TEST(RenderGraph, NullResourcesAreIgnored) {
    RenderGraph graph;
    graph.AddPass({.name = "Optional", .textures = {{rhi::TextureHandle{}, Access::FragmentShaderRead}}}, Nothing);
    EXPECT_TRUE(graph.Plan().front().textures.empty());
}

TEST(RenderGraph, PassesInARowShareTheirGroup) {
    RenderGraph graph;
    graph.AddPass({.name = "Shadows"}, Nothing);
    graph.AddPass({.name = "Emit", .group = "Simulation"}, Nothing);
    graph.AddPass({.name = "Sort", .group = "Simulation"}, Nothing);
    graph.AddPass({.name = "Meshes"}, Nothing);
    const std::vector<PlannedPass> planned = graph.Plan();
    ASSERT_EQ(planned.size(), 4U);
    EXPECT_TRUE(planned[0].startsGroup);
    EXPECT_EQ(planned[0].group, "Shadows");
    EXPECT_TRUE(planned[1].startsGroup);
    EXPECT_EQ(planned[1].group, "Simulation");
    EXPECT_FALSE(planned[2].startsGroup);
    EXPECT_TRUE(planned[3].startsGroup);
}

TEST(RenderGraph, PassesKeepTheirOrder) {
    RenderGraph graph;
    graph.AddPass({.name = "First", .buffers = {{kBuffer, Access::ComputeShaderWrite}}}, Nothing);
    graph.AddPass({.name = "Second", .buffers = {{kBuffer, Access::IndirectRead}}}, Nothing);
    const std::vector<PlannedPass> planned = graph.Plan();
    EXPECT_EQ(planned[0].group, "First");
    EXPECT_EQ(planned[1].group, "Second");
    EXPECT_EQ(graph.PassCount(), 2U);
}
