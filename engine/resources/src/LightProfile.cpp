// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/resources/LightProfile.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace daidalos::resources {

namespace {

constexpr std::string_view kModule = "resources";

core::Error Malformed(std::string_view what) {
    return core::MakeError(core::ErrorCode::ParseError, kModule, std::format("IES profile: {}", what)).value();
}

// The numbers after the TILT line, in order.
class Numbers {
public:
    explicit Numbers(std::string_view text) : m_text(text) {}

    bool Next(double& value) {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == ',' || m_text[m_at] == '\t' ||
                                        m_text[m_at] == '\r' || m_text[m_at] == '\n')) {
            ++m_at;
        }
        if (m_at >= m_text.size()) {
            return false;
        }
        const char* begin = m_text.data() + m_at;
        const char* end = m_text.data() + m_text.size();
        const auto [next, error] = std::from_chars(begin, end, value);
        if (error != std::errc()) {
            return false;
        }
        m_at += static_cast<std::size_t>(next - begin);
        return true;
    }

private:
    std::string_view m_text;
    std::size_t m_at = 0;
};

} // namespace

core::Result<LightProfile> ParseIes(std::string_view text) {
    const std::size_t tilt = text.find("TILT=");
    if (tilt == std::string_view::npos) {
        return tl::unexpected(Malformed("no TILT line"));
    }
    std::size_t lineEnd = text.find('\n', tilt);
    if (lineEnd == std::string_view::npos) {
        return tl::unexpected(Malformed("nothing after the TILT line"));
    }
    std::string_view tiltValue = text.substr(tilt + 5, lineEnd - tilt - 5);
    while (!tiltValue.empty() && (tiltValue.back() == '\r' || tiltValue.back() == ' ')) {
        tiltValue.remove_suffix(1);
    }
    Numbers numbers(text.substr(lineEnd + 1));
    double value = 0.0;
    if (tiltValue == "INCLUDE") {
        // The lamp's output by tilt, which the engine does not use: its geometry, count, angles and factors.
        double count = 0.0;
        if (!numbers.Next(value) || !numbers.Next(count)) {
            return tl::unexpected(Malformed("a short tilt table"));
        }
        for (int skip = 0; skip < static_cast<int>(count) * 2; ++skip) {
            if (!numbers.Next(value)) {
                return tl::unexpected(Malformed("a short tilt table"));
            }
        }
    } else if (tiltValue != "NONE") {
        return tl::unexpected(Malformed("a tilt in another file, which is not supported"));
    }
    double header[13];
    for (double& field : header) {
        if (!numbers.Next(field)) {
            return tl::unexpected(Malformed("a short header"));
        }
    }
    const double multiplier = header[2];
    const auto verticalCount = static_cast<std::size_t>(header[3]);
    const auto horizontalCount = static_cast<std::size_t>(header[4]);
    const auto type = static_cast<int>(header[5]);
    if (type != 1) {
        return tl::unexpected(Malformed(std::format("photometric type {}, where type C (1) is supported", type)));
    }
    if (verticalCount < 2 || horizontalCount < 1 || verticalCount > 10000 || horizontalCount > 10000) {
        return tl::unexpected(Malformed("an odd number of angles"));
    }
    std::vector<double> vertical(verticalCount);
    for (double& angle : vertical) {
        if (!numbers.Next(angle)) {
            return tl::unexpected(Malformed("too few vertical angles"));
        }
    }
    for (std::size_t skip = 0; skip < horizontalCount; ++skip) {
        if (!numbers.Next(value)) {
            return tl::unexpected(Malformed("too few horizontal angles"));
        }
    }
    // Averaged around the axis.
    std::vector<double> candela(verticalCount, 0.0);
    for (std::size_t horizontal = 0; horizontal < horizontalCount; ++horizontal) {
        for (std::size_t index = 0; index < verticalCount; ++index) {
            if (!numbers.Next(value)) {
                return tl::unexpected(Malformed("too few candela values"));
            }
            candela[index] += value * multiplier / static_cast<double>(horizontalCount);
        }
    }
    if (!std::ranges::is_sorted(vertical)) {
        return tl::unexpected(Malformed("vertical angles out of order"));
    }
    LightProfile profile;
    profile.samples.resize(LightProfile::kSamples);
    for (std::size_t sample = 0; sample < LightProfile::kSamples; ++sample) {
        const double angle = 180.0 * static_cast<double>(sample) / static_cast<double>(LightProfile::kSamples - 1);
        double intensity = 0.0;
        if (angle <= vertical.front()) {
            intensity = candela.front();
        } else if (angle >= vertical.back()) {
            // Past the last angle given (a downlight described to 90 degrees), no light.
            intensity = vertical.back() >= 179.9 ? candela.back() : 0.0;
        } else {
            const auto upper = std::ranges::upper_bound(vertical, angle) - vertical.begin();
            const auto lower = static_cast<std::size_t>(upper - 1);
            const double span = vertical[static_cast<std::size_t>(upper)] - vertical[lower];
            const double t = span > 0.0 ? (angle - vertical[lower]) / span : 0.0;
            intensity = candela[lower] + (candela[static_cast<std::size_t>(upper)] - candela[lower]) * t;
        }
        profile.samples[sample] = static_cast<float>(std::max(intensity, 0.0));
    }
    const float peak = *std::ranges::max_element(profile.samples);
    if (!(peak > 0.0f)) {
        return tl::unexpected(Malformed("no light in any direction"));
    }
    for (float& sample : profile.samples) {
        sample /= peak;
    }
    profile.peakCandela = peak;
    return profile;
}

core::Result<LightProfile> LoadIesFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::MakeError(core::ErrorCode::NotFound, kModule,
                               std::format("IES profile '{}' not found", path.generic_string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseIes(text.str());
}

} // namespace daidalos::resources
