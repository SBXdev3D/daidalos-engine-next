// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace daidalos::renderer {

enum class ShaderStage { Vertex, Pixel };

/// Compiles a Slang file with the slangc executable (ADR-0031), with the same options as the build.
/// Fails with ParseError and the compiler's output, which names the file and line, if the shader does
/// not compile, and with IoError if slangc cannot run.
[[nodiscard]] core::Result<std::vector<std::uint32_t>>
CompileShader(const std::filesystem::path& slangc, const std::filesystem::path& source, ShaderStage stage);

/// Detects edits of the shader files in a directory, by modification time.
class ShaderWatcher {
public:
    explicit ShaderWatcher(std::filesystem::path directory);

    /// True when a .slang or .slangh file was added, changed or removed since the previous call.
    [[nodiscard]] bool Poll();

private:
    using Snapshot = std::map<std::filesystem::path, std::filesystem::file_time_type>;

    [[nodiscard]] Snapshot Scan() const;

    std::filesystem::path m_directory;
    Snapshot m_files;
};

} // namespace daidalos::renderer
