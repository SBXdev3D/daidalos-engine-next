// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/rhi/Spirv.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <shaders/RayQueryTest.cs.spv.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "GpuFixture.hpp"

using namespace daidalos;
using gpu_tests::GpuTest;

namespace {

struct RayQueryPush {
    std::uint32_t origins;
    std::uint32_t results;
    std::uint32_t structure;
    std::uint32_t count;
    std::uint32_t mask;
};

// Rays straight down from z = 10: onto the first quad, onto the second, and between them.
constexpr std::array<glm::vec2, 3> kOrigins{glm::vec2(0.0f, 0.0f), glm::vec2(5.0f, 0.0f), glm::vec2(3.0f, 0.0f)};

class AccelerationStructureTest : public GpuTest {
protected:
    void SetUp() override {
        GpuTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        // A 2 by 2 quad around the origin, at z = 0.
        const std::array<glm::vec3, 4> vertices{glm::vec3(-1, -1, 0), glm::vec3(1, -1, 0), glm::vec3(1, 1, 0),
                                                glm::vec3(-1, 1, 0)};
        const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        const rhi::BufferUsage input = rhi::BufferUsage::AccelerationStructureInput | rhi::BufferUsage::TransferDst;
        m_vertices = Created(Device().CreateBuffer({.size = sizeof(vertices), .usage = input, .name = "Quad"}));
        m_indices = Created(Device().CreateBuffer({.size = sizeof(indices), .usage = input, .name = "Quad indices"}));
        ASSERT_TRUE(Device().UploadBuffer(m_vertices, std::as_bytes(std::span(vertices))).has_value());
        ASSERT_TRUE(Device().UploadBuffer(m_indices, std::as_bytes(std::span(indices))).has_value());

        m_quad = Created(Device().CreateAccelerationStructure({.level = rhi::AccelerationStructureLevel::Bottom,
                                                               .geometry = {.vertices = m_vertices,
                                                                            .vertexStride = sizeof(glm::vec3),
                                                                            .vertexCount = 4,
                                                                            .indices = m_indices,
                                                                            .indexCount = 6},
                                                               .name = "Quad"}));
        ASSERT_TRUE(Device().BuildWithUploads(m_quad).has_value());
        m_scene = Created(Device().CreateAccelerationStructure({.level = rhi::AccelerationStructureLevel::Top,
                                                                .maxInstances = 2,
                                                                .rebuiltEveryFrame = true,
                                                                .name = "Scene"}));
        m_instances = Created(Device().CreateBuffer({.size = 2 * rhi::kAccelerationStructureInstanceSize,
                                                     .usage = rhi::BufferUsage::AccelerationStructureInput,
                                                     .memory = rhi::MemoryLocation::CpuToGpu,
                                                     .name = "Instances"}));
        // The first quad where it was made; the second moved 5 m along x and 3 m down, and not opaque.
        m_instanceList = {{
            {.customIndex = 4, .mask = 0x01, .structure = m_quad},
            {.transform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, -3.0f)),
             .customIndex = 9,
             .mask = 0x02,
             .opaque = false,
             .structure = m_quad},
        }};
        Device().WriteInstances(m_instances, 0, m_instanceList);

        m_origins = Created(Device().CreateBuffer({.size = sizeof(kOrigins),
                                                   .usage = rhi::BufferUsage::Storage,
                                                   .memory = rhi::MemoryLocation::CpuToGpu,
                                                   .name = "Origins"}));
        std::memcpy(Device().Mapped(m_origins).data(), kOrigins.data(), sizeof(kOrigins));
        m_results = Created(Device().CreateBuffer({.size = sizeof(glm::vec2) * kOrigins.size(),
                                                   .usage = rhi::BufferUsage::Storage,
                                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                                   .name = "Results"}));
        m_pipeline = Created(
            Device().CreateComputePipeline({.shader = rhi::ToSpirv(kRayQueryTestCsSpirv), .name = "Ray query test"}));
    }

    void TearDown() override {
        if (!IsSkipped()) {
            Device().DestroyPipeline(m_pipeline);
            for (const rhi::BufferHandle buffer : {m_vertices, m_indices, m_instances, m_origins, m_results}) {
                Device().DestroyBuffer(buffer);
            }
            Device().DestroyAccelerationStructure(m_quad);
            Device().DestroyAccelerationStructure(m_scene);
        }
        GpuTest::TearDown();
    }

    template <typename Handle>
    Handle Created(core::Result<Handle> created) {
        EXPECT_TRUE(created.has_value()) << core::Format(created.error());
        return created.value_or(Handle{});
    }

    // Builds the scene and casts the rays with this mask; returns, for each origin, the custom index plus one
    // of what was hit, and the distance.
    std::array<glm::vec2, kOrigins.size()> Trace(std::uint32_t mask) {
        EXPECT_TRUE(Device()
                        .Submit([&](rhi::CommandList& commands) {
                            const std::array<rhi::AccelerationStructureUse, 2> build{
                                {{m_quad, rhi::Access::AccelerationStructureRead},
                                 {m_scene, rhi::Access::AccelerationStructureWrite}}};
                            const rhi::BufferUse instances{m_instances, rhi::Access::AccelerationStructureRead};
                            commands.Use({}, {&instances, 1}, build);
                            commands.BuildAccelerationStructure(m_scene, m_instances, 2);

                            const rhi::AccelerationStructureUse traced{m_scene, rhi::Access::RayQueryRead};
                            const rhi::BufferUse results{m_results, rhi::Access::ComputeShaderWrite};
                            commands.Use({}, {&results, 1}, {&traced, 1});
                            commands.BindPipeline(m_pipeline);
                            commands.Push(RayQueryPush{Device().StorageIndex(m_origins),
                                                       Device().StorageIndex(m_results),
                                                       Device().AccelerationStructureIndex(m_scene),
                                                       static_cast<std::uint32_t>(kOrigins.size()), mask});
                            commands.Dispatch(1);
                        })
                        .has_value());
        std::array<glm::vec2, kOrigins.size()> hits{};
        std::memcpy(hits.data(), Device().Mapped(m_results).data(), sizeof(hits));
        return hits;
    }

    rhi::BufferHandle m_vertices;
    rhi::BufferHandle m_indices;
    rhi::BufferHandle m_instances;
    rhi::BufferHandle m_origins;
    rhi::BufferHandle m_results;
    std::array<rhi::AccelerationStructureInstance, 2> m_instanceList{};
    rhi::AccelerationStructureHandle m_quad;
    rhi::AccelerationStructureHandle m_scene;
    rhi::PipelineHandle m_pipeline;
};

} // namespace

TEST_F(AccelerationStructureTest, RaysHitTheInstancesWhereTheyArePlaced) {
    const auto hits = Trace(0xFF);
    EXPECT_EQ(hits[0].x, 5.0f);
    EXPECT_FLOAT_EQ(hits[0].y, 10.0f);
    EXPECT_EQ(hits[1].x, 10.0f);
    EXPECT_FLOAT_EQ(hits[1].y, 13.0f);
    EXPECT_EQ(hits[2].x, 0.0f) << "Between the quads, the ray hits nothing";
}

TEST_F(AccelerationStructureTest, MaskHidesInstances) {
    const auto hits = Trace(0x01);
    EXPECT_EQ(hits[0].x, 5.0f);
    EXPECT_EQ(hits[1].x, 0.0f) << "The second quad's mask shares no bit with the query's";
}

TEST_F(AccelerationStructureTest, StructuresAreCountedInTheDeviceMemory) {
    const rhi::MemoryStats memory = Device().Memory();
    EXPECT_EQ(memory.accelerationStructures, 2U);
    EXPECT_EQ(memory.accelerationStructureBytes,
              Device().AccelerationStructureSize(m_quad) + Device().AccelerationStructureSize(m_scene));
    EXPECT_GT(Device().AccelerationStructureSize(m_quad), 0U);
}

TEST_F(AccelerationStructureTest, AStructureBuiltWithTheUploadsIsCompactedAndStillHit) {
    const std::uint64_t built = Device().AccelerationStructureSize(m_quad);
    // The first frame submits the uploads that hold the build; the next, once they are done, compacts it.
    for (int frame = 0; frame < 2; ++frame) {
        core::Result<std::optional<rhi::Frame>> begun = Device().BeginFrame();
        ASSERT_TRUE(begun.has_value() && begun->has_value());
        ASSERT_TRUE(Device().EndFrame().has_value());
        ASSERT_TRUE(Device().WaitIdle().has_value());
    }
    EXPECT_LT(Device().AccelerationStructureSize(m_quad), built);
    EXPECT_EQ(Device().Memory().accelerationStructureBytes,
              Device().AccelerationStructureSize(m_quad) + Device().AccelerationStructureSize(m_scene));

    // The instances name the structure by its address, which moved.
    Device().WriteInstances(m_instances, 0, m_instanceList);
    const auto hits = Trace(0xFF);
    EXPECT_EQ(hits[0].x, 5.0f);
    EXPECT_FLOAT_EQ(hits[0].y, 10.0f);
    EXPECT_EQ(hits[1].x, 10.0f);
    EXPECT_FLOAT_EQ(hits[1].y, 13.0f);
    EXPECT_EQ(hits[2].x, 0.0f);
}
