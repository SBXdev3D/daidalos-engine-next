// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "ShaderCompiler.hpp"

#include <daidalos/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
// Must match DAI_SLANG_ARGUMENTS in cmake/DaidalosShaders.cmake.
constexpr std::array<std::string_view, 13> kSlangArguments{"-target",
                                                           "spirv",
                                                           "-profile",
                                                           "spirv_1_6",
                                                           "-capability",
                                                           "spvRayQueryKHR",
                                                           "-fvk-use-entrypoint-name",
                                                           "-matrix-layout-column-major",
                                                           "-O2",
                                                           "-warnings-as-errors",
                                                           "all",
                                                           "-warnings-disable",
                                                           "39001"};

} // namespace

core::Result<std::vector<std::uint32_t>> CompileShader(const std::filesystem::path& slangc,
                                                       const std::filesystem::path& source, ShaderStage stage) {
    const bool vertex = stage == ShaderStage::Vertex;

    std::error_code error;
    const std::filesystem::path outputDirectory = std::filesystem::temp_directory_path(error) / "daidalos-shaders";
    std::filesystem::create_directories(outputDirectory, error);
    if (error) {
        return core::MakeError(
            core::ErrorCode::IoError, kModule,
            std::format("Cannot create '{}': {}", outputDirectory.generic_string(), error.message()));
    }
    const std::filesystem::path output =
        outputDirectory / std::format("{}.{}.spv", source.stem().string(), vertex ? "vs" : "ps");

    std::vector<std::string> arguments{slangc.string()};
    arguments.insert(arguments.end(), kSlangArguments.begin(), kSlangArguments.end());
    arguments.insert(arguments.end(), {"-entry", vertex ? "VSMain" : "PSMain", "-stage", vertex ? "vertex" : "fragment",
                                       "-I", source.parent_path().string(), "-o", output.string(), source.string()});

    DAI_TRY_ASSIGN(platform::ProcessResult compiled, platform::RunProcess(arguments));
    if (compiled.exitCode != 0) {
        return core::MakeError(core::ErrorCode::ParseError, kModule, std::move(compiled.output));
    }

    std::ifstream file(output, std::ios::binary | std::ios::ate);
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot read '{}'", output.generic_string()));
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()),
              static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
    return words;
}

ShaderWatcher::ShaderWatcher(std::filesystem::path directory) : m_directory(std::move(directory)), m_files(Scan()) {}

ShaderWatcher::Snapshot ShaderWatcher::Scan() const {
    Snapshot files;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_directory, error)) {
        const std::filesystem::path extension = entry.path().extension();
        if (extension == ".slang" || extension == ".slangh") {
            // A file being replaced by an editor can vanish between listing and reading: its time
            // stays at the default value and it shows up as changed on the next poll.
            files[entry.path()] = std::filesystem::last_write_time(entry.path(), error);
        }
    }
    return files;
}

bool ShaderWatcher::Poll() {
    Snapshot current = Scan();
    const bool changed = current != m_files;
    m_files = std::move(current);
    return changed;
}

} // namespace daidalos::renderer
