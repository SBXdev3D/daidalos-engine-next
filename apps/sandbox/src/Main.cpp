// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/logging/Log.hpp>

#include <cstdio>

int main() {
    using namespace daidalos;

    logging::LogConfig config;
    config.minimumLevel = logging::Level::Debug;
    config.filePath = "sandbox.log";

    if (const core::Result<void> result = logging::Initialize(config); !result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }

    DAI_LOG_INFO("sandbox", "Daidalos Engine Next {} started", DAI_VERSION);
    DAI_LOG_INFO("sandbox", "Shutting down");

    logging::Shutdown();
    return 0;
}
