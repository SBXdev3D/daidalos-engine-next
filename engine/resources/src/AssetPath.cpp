// Daidalos Engine Next
// Copyright (C) 2011-2026 StÃ©phane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/resources/AssetPath.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

std::filesystem::path FromUtf8(std::string_view text) {
    return {std::u8string(text.begin(), text.end())};
}

} // namespace

std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

core::Result<std::string> NormalizeAssetPath(std::string_view path) {
    std::string separators(path);
    std::ranges::replace(separators, '\\', '/');

    if (separators.empty()) {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule), "An asset path cannot be empty");
    }
    const std::filesystem::path parsed = FromUtf8(separators);
    if (parsed.is_absolute() || parsed.has_root_name() || separators.front() == '/') {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule),
                               std::format("Asset path '{}' must be relative to the asset root", path));
    }

    const std::filesystem::path normal = parsed.lexically_normal();
    const std::string text = ToUtf8(normal);
    if (text == "." || text.empty() || text.back() == '/') {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule),
                               std::format("Asset path '{}' does not name a file", path));
    }
    if (*normal.begin() == "..") {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule),
                               std::format("Asset path '{}' leaves the asset root", path));
    }
    return text;
}

core::Result<std::filesystem::path> ResolveAssetPath(const std::filesystem::path& root,
                                                     std::string_view normalizedPath) {
    std::filesystem::path current = root;
    for (const std::filesystem::path& component : FromUtf8(normalizedPath)) {
        std::error_code error;
        std::filesystem::directory_iterator entries(current, error);
        if (error) {
            return core::MakeError(
                core::ErrorCode::NotFound, std::string(kModule),
                std::format("Asset '{}' not found: cannot read directory '{}'", normalizedPath, ToUtf8(current)));
        }

        const std::string wanted = ToUtf8(component);
        bool found = false;
        std::string differentCase;
        for (; entries != std::filesystem::directory_iterator(); entries.increment(error)) {
            if (error) {
                break;
            }
            const std::string name = ToUtf8(entries->path().filename());
            if (name == wanted) {
                found = true;
                break;
            }
            const auto lower = [](char letter) {
                return (letter >= 'A' && letter <= 'Z') ? static_cast<char>(letter - 'A' + 'a') : letter;
            };
            if (std::ranges::equal(name, wanted, {}, lower, lower)) {
                differentCase = name;
            }
        }
        if (!found && !differentCase.empty()) {
            return core::MakeError(
                core::ErrorCode::InvalidArgument, std::string(kModule),
                std::format("Asset '{}': '{}' is spelled '{}' on disk", normalizedPath, wanted, differentCase));
        }
        if (!found) {
            return core::MakeError(core::ErrorCode::NotFound, std::string(kModule),
                                   std::format("Asset '{}' not found under '{}'", normalizedPath, ToUtf8(root)));
        }
        current /= component;
    }
    return current;
}

} // namespace daidalos::resources
