// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/rhi/Types.hpp>

#include <gtest/gtest.h>

namespace {

using daidalos::rhi::Access;
using daidalos::rhi::AccessOf;
using daidalos::rhi::AccessState;
using daidalos::rhi::AccessStep;
using daidalos::rhi::CurrentAccess;
using daidalos::rhi::NextAccess;
using daidalos::rhi::TextureState;

constexpr bool kTexture = true;
constexpr bool kBuffer = false;

} // namespace

TEST(NextAccess, NewTextureLeavesTheUndefinedLayoutWithABarrier) {
    AccessState state;
    const AccessStep step = NextAccess(state, Access::ColorTarget, kTexture);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(step.source, Access::None);
    EXPECT_EQ(state.lastWrite, Access::ColorTarget);
}

TEST(NextAccess, BufferWithoutAKnownUseWaitsForEverything) {
    AccessState state;
    const AccessStep step = NextAccess(state, Access::ComputeShaderRead, kBuffer);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(step.source, Access::None);
    EXPECT_EQ(state.reads, Access::ComputeShaderRead);
}

TEST(NextAccess, ReadAfterWriteWaitsForTheWrite) {
    AccessState state{.lastWrite = Access::ComputeShaderWrite};
    const AccessStep step = NextAccess(state, Access::IndirectRead, kBuffer);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(step.source, Access::ComputeShaderWrite);
    EXPECT_EQ(state.reads, Access::IndirectRead);
}

TEST(NextAccess, RepeatedReadNeedsNoBarrier) {
    AccessState state{.lastWrite = Access::ComputeShaderWrite, .reads = Access::FragmentShaderRead};
    EXPECT_FALSE(NextAccess(state, Access::FragmentShaderRead, kBuffer).barrier);
    EXPECT_EQ(state.reads, Access::FragmentShaderRead);
}

TEST(NextAccess, ReadInANewStageWaitsForTheWriteAgain) {
    // The write was made visible to fragment shaders only: a compute shader must wait for it too.
    AccessState state{.lastWrite = Access::ColorTarget, .reads = Access::FragmentShaderRead};
    const AccessStep step = NextAccess(state, Access::ComputeShaderRead, kTexture);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(step.source, Access::ColorTarget | Access::FragmentShaderRead);
    EXPECT_EQ(state.reads, Access::FragmentShaderRead | Access::ComputeShaderRead);
}

TEST(NextAccess, WriteAfterReadsWaitsForEveryRead) {
    AccessState state{.lastWrite = Access::ColorTarget,
                      .reads = Access::FragmentShaderRead | Access::ComputeShaderRead};
    const AccessStep step = NextAccess(state, Access::ColorTarget, kTexture);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(step.source, Access::ColorTarget | Access::FragmentShaderRead | Access::ComputeShaderRead);
    EXPECT_EQ(state, (AccessState{.lastWrite = Access::ColorTarget}));
}

TEST(NextAccess, WriteAfterWriteWaits) {
    AccessState state{.lastWrite = Access::ColorTarget};
    EXPECT_TRUE(NextAccess(state, Access::ColorTarget, kTexture).barrier);
}

TEST(NextAccess, LayoutChangeBetweenReadsWaits) {
    AccessState state{.lastWrite = Access::TransferWrite, .reads = Access::TransferRead};
    const AccessStep step = NextAccess(state, Access::FragmentShaderRead, kTexture);
    EXPECT_TRUE(step.barrier);
    EXPECT_EQ(CurrentAccess(state), Access::FragmentShaderRead);
}

TEST(NextAccess, DiscardWaitsEvenForTheSameRead) {
    AccessState state{.lastWrite = Access::ColorTarget, .reads = Access::FragmentShaderRead};
    EXPECT_TRUE(NextAccess(state, Access::FragmentShaderRead, kTexture, true).barrier);
}

TEST(AccessOf, ShaderReadCoversEveryShaderStage) {
    EXPECT_EQ(AccessOf(TextureState::ShaderRead),
              Access::VertexShaderRead | Access::FragmentShaderRead | Access::ComputeShaderRead);
    EXPECT_EQ(AccessOf(TextureState::Undefined), Access::None);
}
