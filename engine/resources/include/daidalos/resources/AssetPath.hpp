// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace daidalos::resources {

/// The identity of an asset: a path relative to an asset root, in UTF-8, with forward slashes and
/// without "." or ".." (ADR-0016). Two spellings of a file give the same string, and the case is kept
/// because the case of file names matters on Linux. Fails with InvalidArgument on an empty path, an
/// absolute path, a path with a ':', or a path that leaves the root. The result is the same on every
/// platform.
[[nodiscard]] core::Result<std::string> NormalizeAssetPath(std::string_view path);

/// Finds a normalized asset path under the root. The case must match the files on disk on every
/// platform: a mismatch is InvalidArgument, so that it cannot go unnoticed on Windows and break on
/// Linux. A file that does not exist is NotFound.
[[nodiscard]] core::Result<std::filesystem::path> ResolveAssetPath(const std::filesystem::path& root,
                                                                   std::string_view normalizedPath);

/// The path in UTF-8 with forward slashes, whatever the system's code page can represent.
[[nodiscard]] std::string ToUtf8(const std::filesystem::path& path);

} // namespace daidalos::resources
