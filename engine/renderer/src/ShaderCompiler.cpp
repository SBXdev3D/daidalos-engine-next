// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

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
// Must match dai_add_shaders in cmake/DaidalosShaders.cmake.
constexpr std::array<std::string_view, 3> kDxcArguments{"-spirv", "-fspv-target-env=vulkan1.3", "-Zpc"};

} // namespace

core::Result<std::vector<std::uint32_t>> CompileShader(const std::filesystem::path& dxc,
                                                       const std::filesystem::path& source, ShaderStage stage) {
    const bool vertex = stage == ShaderStage::Vertex;

    std::error_code error;
    const std::filesystem::path outputDirectory = std::filesystem::temp_directory_path(error) / "daidalos-shaders";
    std::filesystem::create_directories(outputDirectory, error);
    if (error) {
        return core::MakeError(
            core::ErrorCode::IoError, std::string(kModule),
            std::format("Cannot create '{}': {}", outputDirectory.generic_string(), error.message()));
    }
    const std::filesystem::path output =
        outputDirectory / std::format("{}.{}.spv", source.stem().string(), vertex ? "vs" : "ps");

    std::vector<std::string> arguments{dxc.string()};
    arguments.insert(arguments.end(), kDxcArguments.begin(), kDxcArguments.end());
    arguments.insert(arguments.end(), {"-T", vertex ? "vs_6_7" : "ps_6_7", "-E", vertex ? "VSMain" : "PSMain", "-I",
                                       source.parent_path().string(), "-Fo", output.string(), source.string()});

    core::Result<platform::ProcessResult> compiled = platform::RunProcess(arguments);
    if (!compiled) {
        return tl::unexpected(std::move(compiled.error()));
    }
    if (compiled->exitCode != 0) {
        return core::MakeError(core::ErrorCode::ParseError, std::string(kModule), std::move(compiled->output));
    }

    std::ifstream file(output, std::ios::binary | std::ios::ate);
    if (!file) {
        return core::MakeError(core::ErrorCode::IoError, std::string(kModule),
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
        if (extension == ".hlsl" || extension == ".hlsli") {
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
