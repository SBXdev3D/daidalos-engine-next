// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/rhi/Device.hpp>

#include <gtest/gtest.h>

#include <optional>

namespace daidalos::gpu_tests {

/// Creates a headless device with validation, or skips the test on machines without Vulkan 1.3.
/// Any validation error fails the test.
class GpuTest : public ::testing::Test {
protected:
    void SetUp() override {
        rhi::DeviceConfig config;
        config.validation = true;
        core::Result<rhi::Device> created = rhi::Device::Create(config);
        if (!created) {
            ASSERT_EQ(created.error().code, core::ErrorCode::NotSupported) << core::Format(created.error());
            GTEST_SKIP() << core::Format(created.error());
        }
        m_device.emplace(std::move(*created));
    }

    void TearDown() override {
        if (m_device.has_value()) {
            EXPECT_EQ(m_device->ValidationErrorCount(), 0U);
        }
    }

    [[nodiscard]] rhi::Device& Device() { return *m_device; }

private:
    std::optional<rhi::Device> m_device;
};

} // namespace daidalos::gpu_tests
